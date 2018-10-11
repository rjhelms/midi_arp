#include <MIDI.h>
#include <SoftwareSerial.h>

#define TEMPO_LED       13
#define DISPLAY_DATA    14  // both data lines of 74164 tied together
#define DISPLAY_CLOCK   15
#define DISPLAY_CC_1    17  // cathode pin for digit 1
#define DISPLAY_CC_2    18  // cathode pin for digit 2
#define DISPLAY_CC_3    19  // cathode pin for digit 3
#define DISPLAY_MX_TIME 5   // multiplexing rate for display, in millis
#define CLOCK_SERIAL_RX 2
#define CLOCK_SERIAL_TX 3
#define NOTE_SERIAL_RX  4
#define NOTE_SERIAL_TX  5

#define DEFAULT_CHANNEL 1

#define NOTE_ARRAY_SIZE 12
#define INVALID_NOTE    255 // used as an indicator that no note is in the current position
                            // since valid midi notes range from 0-127, any value > 127 can work

byte display_cc[3] = {DISPLAY_CC_1, DISPLAY_CC_2, DISPLAY_CC_3}; 
char display_chars[4];
byte display_current = 0;
unsigned long display_next_time = 0;

char characters[128]; // array to hold character values

byte current_tick = 0;
byte notes_on = 0;


byte last_note = INVALID_NOTE;
byte current_note_index = 0;

bool led_on = false;
bool running = false;
SoftwareSerial swSerial1(CLOCK_SERIAL_RX, CLOCK_SERIAL_TX);
SoftwareSerial swSerial2(NOTE_SERIAL_RX, NOTE_SERIAL_TX);
MIDI_CREATE_INSTANCE(SoftwareSerial, swSerial1, ClockMIDI);
MIDI_CREATE_INSTANCE(HardwareSerial, Serial, NoteMIDI);
byte notes[NOTE_ARRAY_SIZE];

void setup() {
  // configure display
  pinMode(TEMPO_LED, OUTPUT);
  pinMode(DISPLAY_DATA, OUTPUT);
  pinMode(DISPLAY_CLOCK, OUTPUT);
  for (int i = 0; i < 3; i++)
  {
    pinMode(display_cc[i], OUTPUT);
  }
  initCharacters();
  strncpy(display_chars, "OFF", 3);
  
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
    switch(NoteMIDI.getType())
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
  
  if (millis() > display_next_time)
    doDisplay();
}

void handleStart()
{
  running = true;
  // kill all notes at start
  NoteMIDI.sendControlChange(123,0, DEFAULT_CHANNEL);
  // send start
  NoteMIDI.sendRealTime(midi::Start);
  current_tick = 0;
  strncpy(display_chars, "ON ", 3);
}

void handleStop()
{
  running = false;
  // kill all notes at end
  NoteMIDI.sendControlChange(123,0, DEFAULT_CHANNEL);
  // send stop
  NoteMIDI.sendRealTime(midi::Stop);
  last_note = INVALID_NOTE;
  current_note_index = 0;
  current_tick = 0;
  led_on = false;
  strncpy(display_chars, "OFF", 3);
}

void handleClock()
{
  // send clock
  NoteMIDI.sendRealTime(midi::Clock);
  // process clock
  if (running)
  {
    if (current_tick == 0)
    {
      led_on = true;
      // TODO: configurable clocking & gating - this is quarter notes, 50% gating
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
    else if (current_tick == 5)
    {
      led_on = false;
    }
    else if (current_tick == 12)
    {
      // if a valid note is playing
      if (last_note != INVALID_NOTE)
      {
        NoteMIDI.sendNoteOff(last_note, 127, DEFAULT_CHANNEL);
        last_note = INVALID_NOTE;
      }
    }
    current_tick++;
    current_tick = current_tick % 24;
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

void doDisplay()
{
  for (int i = 0; i < 3; i++)
  {
    digitalWrite(display_cc[i], HIGH);
  }
  shiftOut(DISPLAY_DATA, DISPLAY_CLOCK, MSBFIRST, characters[display_chars[display_current]]);
  digitalWrite(display_cc[display_current], LOW);
  display_current++;
  display_current %= 3;
  display_next_time = millis() + DISPLAY_MX_TIME;
}

// TODO: move this into progmem somehow
void initCharacters()
{
  characters['0'] = 0xEB;
  characters['1'] = 0x28;
  characters['2'] = 0xB3;
  characters['3'] = 0xBA;
  characters['4'] = 0x78;
  characters['5'] = 0xDA;
  characters['6'] = 0xDB;
  characters['7'] = 0xA8;
  characters['8'] = 0xFB;
  characters['9'] = 0xFA;
  characters['A'] = 0xF9;
  characters['B'] = 0x5B;
  characters['C'] = 0x13;
  characters['D'] = 0x3B;
  characters['E'] = 0xD3;
  characters['F'] = 0xD1;
  characters['G'] = 0xCB;
  characters['H'] = 0x59;
  characters['I'] = 0x08;
  characters['J'] = 0x2A;
  characters['K'] = 0x79;
  characters['L'] = 0x43;
  characters['M'] = 0x89;
  characters['N'] = 0x19;
  characters['O'] = 0x1B;
  characters['P'] = 0xF1;
  characters['Q'] = 0xF8;
  characters['R'] = 0x11;
  characters['S'] = 0xDA;
  characters['T'] = 0x53;
  characters['U'] = 0x6B;
  characters['V'] = 0x0B;
  characters['W'] = 0x72;
  characters['X'] = 0x7D;
  characters['Y'] = 0x7A;
  characters['Z'] = 0xB3;
  characters['-'] = 0x10;
  characters['\''] = 0x20;
  characters[','] = 0x01;
  characters['.'] = 0x04;
  characters['\"'] = 0x60;
  characters['='] = 0x12;
  characters['('] = 0xC3;
  characters[')'] = 0xAA;
  characters['['] = 0xC3;
  characters[']'] = 0xAA;
  characters['{'] = 0xC3;
  characters['}'] = 0xAA;
  characters['|'] = 0x41;
  characters['_'] = 0x02;
  characters['<'] = 0x03;
  characters['>'] = 0xA0;
  characters[':'] = 0x90;
  characters['^'] = 0x80;
  characters['@'] = 0xBB;
  characters['`'] = 0x40;
  characters['!'] = 0x24;
  characters[';'] = 0x0C;
  characters['\\'] = 0x58;
  characters['/'] - 0x31;   // this one doesn't want to work
  characters['$'] = 0xDA;   // just an S
  characters['#'] = 0x7B;
  characters['+'] = 0x51;
  characters['&'] = 0x38;
  characters['*'] = 0xC0;
  characters['?'] = 0xB4;
  characters['%'] = 0x48;
}

