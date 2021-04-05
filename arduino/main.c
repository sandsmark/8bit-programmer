#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define SHIFT_DATA 2
#define SHIFT_CLK 3
#define SHIFT_LATCH 4

#define BUS_OUT 5

// Pin 6 is LED_BUILTIN on some boards

#define HALT 7
#define RESET 8

#define ROM_DISCONNECT 9

#define RAM_CLK 10

#define ADDR_WRITE 11
#define VAL_WRITE 12

enum STEP {
    ADDRESS_FIRST,
    ADDRESS_LAST,
    VALUE_FIRST,
    VALUE_LAST,
    COMPLETE,
};

void resetPinState()
{
    // Ensure everything is default state
    digitalWrite(ROM_DISCONNECT, HIGH);
    digitalWrite(RAM_CLK, LOW);
    digitalWrite(ADDR_WRITE, LOW);
    digitalWrite(VAL_WRITE, LOW);
    digitalWrite(BUS_OUT, LOW);
}

void ensureHalted()
{
    digitalWrite(LED_BUILTIN, HIGH);

    digitalWrite(HALT, HIGH);

    resetPinState();
}

void run()
{
    resetPinState();

    // Reset CPU
    digitalWrite(RESET, LOW);
    digitalWrite(RESET, HIGH);
    digitalWrite(RESET, LOW);

    // Start clock
    digitalWrite(HALT, LOW);

    // Re-enable normal microcode execution
    digitalWrite(ROM_DISCONNECT, LOW);

    digitalWrite(LED_BUILTIN, LOW);
}

void putOnBus(const uint8_t val)
{
    ensureHalted(); // justincase, also because I'm lazy

    shiftOut(SHIFT_DATA, SHIFT_CLK, MSBFIRST, val);
    digitalWrite(SHIFT_LATCH, LOW);
    digitalWrite(SHIFT_LATCH, HIGH);
    digitalWrite(SHIFT_LATCH, LOW);

    digitalWrite(SHIFT_LATCH, LOW);
}

void setAddress(const uint8_t address)
{
    putOnBus(address);

    digitalWrite(ADDR_WRITE, HIGH);
    digitalWrite(RAM_CLK, LOW);
    digitalWrite(RAM_CLK, HIGH);
    delay(10); // idk, justincase
    digitalWrite(RAM_CLK, LOW);

    digitalWrite(ADDR_WRITE, LOW);
}

void setValue(const uint8_t value)
{
    putOnBus(value);

    digitalWrite(VAL_WRITE, HIGH);
    digitalWrite(RAM_CLK, LOW);
    digitalWrite(RAM_CLK, HIGH);
    delay(10); // idk, justincase
    digitalWrite(RAM_CLK, LOW);

    digitalWrite(VAL_WRITE, LOW);
}

static uint8_t address;
static uint8_t value;
static uint8_t state;

void loop()
{
    // TODO: do I need to do things here? Or is it safe to latch out stuff in
    // serialEvent()? Nobody knows, arduino is weird
}

void serialEvent()
{
    while (Serial.available()) {
        const char c = Serial.read();

        switch(c) {
            case '\n':
                address = 0;
                value = 0;
                state = ADDRESS_FIRST;
                continue;
            case ' ':
                value = 0;
                state = VALUE_FIRST;
                continue;
            case 'R': // Run, reset, whatever
                run();
            default:
                break;
        }

        uint8_t num = 0;
        if (c >= '0' && c <= '9') {
            num = c - '0';
        } else if (c >= 'A' && c <= 'F') {
            num = (c - 'A') + 0xA;
        } else if (c >= 'a' && c <= 'f') {
            num = (c - 'a') + 0xa;
        } else {
            Serial.print("Invalid char:");
            Serial.print(c, HEX);
            Serial.print("\n");
            continue;
        }

        switch (state) {
            case ADDRESS_FIRST:
                address = num;
                break;
            case ADDRESS_LAST:
                address |= num << 4;
                setAddress(address);
                break;
            case VALUE_FIRST:
                value = num;
                break;
            case VALUE_LAST:
                value |= num << 4;
                setValue(address);
                break;
            default:
                Serial.print("Invalid state ");
                Serial.print(state);
                Serial.print("\n");
                continue;
        }

        if (state == VALUE_LAST) {
            Serial.print("Address: ");
            Serial.print(address, HEX);
            Serial.print(" Value: ");
            Serial.print(value, HEX);
            Serial.print("\n");
        }

        state++;
    }
}


void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);

    pinMode(SHIFT_DATA, OUTPUT);
    pinMode(SHIFT_CLK, OUTPUT);
    pinMode(SHIFT_LATCH, OUTPUT);

    pinMode(BUS_OUT, OUTPUT);

    pinMode(HALT, OUTPUT);
    pinMode(RESET, OUTPUT);

    pinMode(ROM_DISCONNECT, OUTPUT);

    pinMode(RAM_CLK, OUTPUT);

    pinMode(ADDR_WRITE, OUTPUT);
    pinMode(VAL_WRITE, OUTPUT);

    Serial.begin(57600);

    Serial.println("Starting");
}
