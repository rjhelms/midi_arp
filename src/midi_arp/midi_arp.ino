#include <MIDI.h>
#include <SoftwareSerial.h>
#include <Encoder.h>

#define TEMPO_LED       13
#define DISPLAY_DATA    14  // both data lines of 74164 tied together
#define DISPLAY_CLOCK   15
#define DISPLAY_CC_1    17  // cathode pin for digit 1
#define DISPLAY_CC_2    18  // cathode pin for digit 2
#define DISPLAY_CC_3    19  // cathode pin for digit 3
#define DISPLAY_MX_TIME 2   // multiplexing rate for display, in millis
#define ENCODER_A       2
#define ENCODER_B       3
#define SW_ENTER        6
#define SW_BACK         7
#define DEBOUNCE_TIME   10

#define CLOCK_SERIAL_RX 4
#define CLOCK_SERIAL_TX 5

#define DEFAULT_CHANNEL 1

#define NOTE_ARRAY_SIZE 12
#define INVALID_NOTE    255 // used as an indicator that no note is in the current position
                            // since valid midi notes range from 0-127, any value > 127 can work

byte display_cc[3] = {DISPLAY_CC_1, DISPLAY_CC_2, DISPLAY_CC_3};
char display_chars[4];
byte display_current = 0;
unsigned long display_next_time = 0;

const PROGMEM char characters[] = {
  // 7-segment characters for 7-byte ASCII
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x24, 0x60, 0x7B, 0xDA, 0x48, 0x38, 0x20, 0xC3, 0xAA, 0xC0, 0x51, 0x01, 0x10, 0x04, 0x31,
  0xEB, 0x28, 0xB3, 0xBA, 0x78, 0xDA, 0xDB, 0xA8, 0xFB, 0xFA, 0x90, 0x0C, 0x03, 0x12, 0xA0, 0xB4,
  0xBB, 0xF9, 0x5B, 0x13, 0x3B, 0xD3, 0xD1, 0xCB, 0x59, 0x08, 0x2A, 0x79, 0x43, 0x89, 0x19, 0x1B,
  0xF1, 0xF8, 0x11, 0xDA, 0x53, 0x6B, 0x0B, 0x72, 0x7D, 0x7A, 0xB3, 0xC3, 0x60, 0xAA, 0x80, 0x02,
  0x40, 0xF9, 0x5B, 0x13, 0x3B, 0xD3, 0xD1, 0xCB, 0x59, 0x08, 0x2A, 0x79, 0x43, 0x89, 0x19, 0x1B,
  0xF1, 0xF8, 0x11, 0xDA, 0x53, 0x6B, 0x0B, 0x72, 0x7D, 0x7A, 0xB3, 0xC3, 0x41, 0xAA, 0xE0, 0x00
};

const char division_text[17][4] = {
  " 1D", " 1 ", " 2D", " 1T", " 2 ", " 4D", " 2T", " 4 ",
  " 8D", " 4T", " 8 ", "16D", " 8T", "16 ","16T", "32 ",
  "32T"
};
const byte division_ticks[] = {
  144, 96, 72, 64, 48, 36, 32, 24,
  18, 16, 12, 9, 8, 6, 4, 3,
  2
};

int division_current = 7;
int division_display = 7;
byte sync_tick = 0;
byte arp_tick = 0;
byte notes_on = 0;

byte last_note = INVALID_NOTE;
byte current_note_index = 0;

bool led_on = false;
bool running = false;

bool enter_state;
unsigned long enter_debounce_time = 0;
bool back_state;
unsigned long back_debounce_time = 0;

SoftwareSerial swSerial1(CLOCK_SERIAL_RX, CLOCK_SERIAL_TX);
MIDI_CREATE_INSTANCE(SoftwareSerial, swSerial1, ClockMIDI);
MIDI_CREATE_INSTANCE(HardwareSerial, Serial, NoteMIDI);
byte notes[NOTE_ARRAY_SIZE];

Encoder encoder(ENCODER_A, ENCODER_B);
int encoder_position = 0;

void setup() {
  // configure display
  pinMode(TEMPO_LED, OUTPUT);
  pinMode(DISPLAY_DATA, OUTPUT);
  pinMode(DISPLAY_CLOCK, OUTPUT);
  for (int i = 0; i < 3; i++)
  {
    pinMode(display_cc[i], OUTPUT);
  }

  // configure buttons
  pinMode(SW_ENTER, INPUT_PULLUP);
  pinMode(SW_BACK, INPUT_PULLUP);
  enter_state = digitalRead(SW_ENTER);
  back_state = digitalRead(SW_BACK);

  // enable midi
  ClockMIDI.begin(MIDI_CHANNEL_OMNI);
  NoteMIDI.begin(DEFAULT_CHANNEL);

  // initialize handlers etc
  // TODO: handle clock from both sources
  ClockMIDI.setHandleClock(handleClock);
  ClockMIDI.setHandleStart(handleStart);
  ClockMIDI.setHandleStop(handleStop);
  ClockMIDI.turnThruOff();
  NoteMIDI.turnThruOn(midi::Thru::DifferentChannel);

  // initialize note array
  for (int i = 0; i < NOTE_ARRAY_SIZE; i++)
  {
    notes[i] = INVALID_NOTE;
  }
}

void loop() {
  while (NoteMIDI.read())
  {
    switch (NoteMIDI.getType())
    {
      case midi::NoteOff:
        handleNoteOff(NoteMIDI.getChannel(), NoteMIDI.getData1(), NoteMIDI.getData2());
        break;
      case midi::NoteOn:
        handleNoteOn(NoteMIDI.getChannel(), NoteMIDI.getData1(), NoteMIDI.getData2());
        break;
      default:
        NoteMIDI.send(NoteMIDI.getType(), NoteMIDI.getData1(), NoteMIDI.getData2(), NoteMIDI.getChannel());
        break;
    }
  }
  ClockMIDI.read();
  digitalWrite(TEMPO_LED, led_on);
  handleEncoder();
  handleButtons();
  strncpy(display_chars, division_text[division_display], 3);
  if (millis() >= display_next_time)
    doDisplay();
}

void handleEncoder()
{
  encoder_position = encoder.read();
  if (encoder_position > 3)
  {
    processEncoder(1);
  } else if (encoder_position < -3) {
    processEncoder(-1);
  }
}

void handleButtons()
{
  bool new_enter_state = digitalRead(SW_ENTER);
  if (new_enter_state != enter_state)
  {
    if (enter_debounce_time == 0)
    {
      enter_debounce_time = millis();
      enter_state = new_enter_state;
      if (enter_state == LOW)
      {
        processEnter();
      }
    }
  }
  if (millis() - DEBOUNCE_TIME >= enter_debounce_time)
  {
    enter_debounce_time = 0;
  }
  
  bool new_back_state = digitalRead(SW_BACK);
  if (new_back_state != back_state)
  {
    if (back_debounce_time == 0)
    {
      back_debounce_time = millis();
      back_state = new_back_state;
      if (back_state == LOW)
      {
        processBack();
      }
    }
  }
  if (millis() - DEBOUNCE_TIME >= back_debounce_time)
  {
    back_debounce_time = 0;
  }
}

void handleStart()
{
  running = true;
  // kill all notes at start
  NoteMIDI.sendControlChange(123, 0, DEFAULT_CHANNEL);
  // send start
  NoteMIDI.sendRealTime(midi::Start);
  sync_tick = 0;
  arp_tick = 0;
}

void handleStop()
{
  running = false;
  // kill all notes at end
  NoteMIDI.sendControlChange(123, 0, DEFAULT_CHANNEL);
  // send stop
  NoteMIDI.sendRealTime(midi::Stop);
  last_note = INVALID_NOTE;
  current_note_index = 0;
  sync_tick = 0;
  arp_tick = 0;
  led_on = false;
}

void handleClock()
{
  // send clock
  NoteMIDI.sendRealTime(midi::Clock);
  // TODO: more meaningful start/stop
  // process clock
  if (running)
  {
    // TODO: actual syncing
    if (sync_tick == 0)
    {
      led_on = true;
    }
    else if (sync_tick == 5)
    {
      led_on = false;
    }

    if (arp_tick == 0)
    {
      // TODO: configurable gating - for now, 100%
      if (last_note != INVALID_NOTE)
      {
        NoteMIDI.sendNoteOff(last_note, 127, DEFAULT_CHANNEL);
        last_note = INVALID_NOTE;
      }
      // if we've got notes held down...
      if (notes_on > 0)
      {
        if (notes[current_note_index] == INVALID_NOTE)
          current_note_index = 0;
        last_note = notes[current_note_index];
        NoteMIDI.sendNoteOn(last_note, 127, DEFAULT_CHANNEL);
        current_note_index++;
        current_note_index = current_note_index % NOTE_ARRAY_SIZE;
      } else {
        // reset counter
        current_note_index = 0;
      }
    }
    sync_tick++;
    sync_tick %= 24;
    arp_tick++;
    arp_tick %= division_ticks[division_current];
  }
}

void handleNoteOn(byte channel, byte note, byte velocity)
{
  if (channel == DEFAULT_CHANNEL)
  {
    addNote(note, velocity);
  }
  if (!running) {
    NoteMIDI.sendNoteOn(note, velocity, channel);
  }
}

void handleNoteOff(byte channel, byte note, byte velocity)
{
  removeNote(note, velocity);
  if (!running) {
    NoteMIDI.sendNoteOff(note, velocity, channel);
  }
}

void addNote(byte note, byte velocity)
{
  // find the first invalid note, replace with new note, and sort
  for (int i = 0; i < NOTE_ARRAY_SIZE; i++)
  {
    if (notes[i] == INVALID_NOTE)
    {
      notes[i] = note;
      sortNotes();
      notes_on++;
      return;
    }
  }
}

void removeNote(byte note, byte velocity)
{
  // replace each instance of the current note with INVALID_NOTE, and sort
  for (int i = 0; i < NOTE_ARRAY_SIZE; i++)
  {
    if (notes[i] == note)
    {
      notes[i] = INVALID_NOTE;
      notes_on--;
    }
  }
  sortNotes();
}

void isort(byte *a, int n)
{
  for (int i = 1; i < n; ++i)
  {
    int j = a[i];
    int k;
    for (k = i - 1; (k >= 0) && (j < a[k]); k--)
    {
      a[k + 1] = a[k];
    }
    a[k + 1] = j;
  }
}

void sortNotes()
{
  isort(notes, NOTE_ARRAY_SIZE);
}

// TODO: navigation and display modes

void doDisplay()
{
  // TODO: fix display jitter
  digitalWrite(display_cc[display_current], HIGH);
  display_current++;
  display_current %= 3;
  shiftOut(DISPLAY_DATA, DISPLAY_CLOCK, MSBFIRST, pgm_read_byte_near(characters + display_chars[display_current]));
  digitalWrite(display_cc[display_current], LOW);
  display_next_time += DISPLAY_MX_TIME;
}

void processEncoder(int movement)
{
  division_display += movement;
  if (division_display < 0)
    division_display = 0;
  if (division_display >= sizeof(division_ticks))
    division_display = sizeof(division_ticks) - 1;
  encoder.write(0);
}

void processEnter()
{
  division_current = division_display;
}

void processBack()
{
  division_display = division_current;
}

