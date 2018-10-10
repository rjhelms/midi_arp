#include <MIDI.h>
#include <SoftwareSerial.h>

#define TEMPO_LED 13

#define CLOCK_SERIAL_RX 2
#define CLOCK_SERIAL_TX 3
#define NOTE_SERIAL_RX 4
#define NOTE_SERIAL_TX 5
#define DEFAULT_CHANNEL 1
#define NOTE_ARRAY_SIZE 12

#define INVALID_NOTE 255  // used as an indicator that no note is in the current position
                          // since valid midi notes range from 0-127, any value > 127 can work

int current_tick = 0;
int notes_on = 0;
long last_micros = 0;

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

  // initialize array
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
}

void handleStart()
{
  running = true;
  // kill all notes at start
  NoteMIDI.sendControlChange(123,0, DEFAULT_CHANNEL);
  // send start
  NoteMIDI.sendRealTime(midi::Start);
  current_tick = 0;
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

