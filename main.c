#define F_CPU 1.2e6

#define LIGTH_THRESHOLD   0xBF

#define UPPER_LIMIT 10
#define CONFIDENCE_LIMIT 7
#define LOWER_LIMIT 0

#define TOKENS_MAX 900

#define FIREFLY0_ENABLED (1<<0)
#define FIREFLY1_ENABLED (1<<1)

#define FIREFLY0_OFFSET (1<<2)
#define FIREFLY1_OFFSET (1<<3)

#define FIREFLY0_INTENSITY (1<<4)
#define FIREFLY1_INTENSITY (1<<5)

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <avr/sleep.h>
#include <avr/pgmspace.h>

#define WDP_32MS (1<<WDP0)
#define WDP_1S  ((1<<WDP2) | (1<<WDP1))
#define WDP_2S  ((1<<WDP2) | (1<<WDP1) | (1<<WDP0))
#define WDP_8S  ((1<<WDP3) | (1<<WDP0))

#define SEQ_SIZE 22

volatile uint8_t sleep_interval;

int8_t darkness;
int16_t firefly_tokens;

// Duty cycle sequence of the firefly
uint8_t const sequence[SEQ_SIZE] PROGMEM = {
	0, 0, 90, 168, 223, 252, 255, 236, 202, 162, 122, 86, 58, 37, 22, 12, 7, 3, 2, 1, 0, 0
};

uint16_t random() {
	static uint16_t lfsr=0xcafe;

	// http://en.wikipedia.org/wiki/Linear_feedback_shift_register
	lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xB400u);
	return lfsr;
}

void setupPWM() {
	// Set Timer 0 prescaler
	TCCR0B |= (1 << CS00);
	// Set to 'Fast PWM' mode
	TCCR0A |= (1 << WGM01) | (1 << WGM00);
	// Clear OC0A (PB0) output on compare match, upwards counting.
	// Set duty cycle to 0
	OCR0B = 0;
	OCR0A = 0;
}

void firefly1(uint8_t val) {
	OCR0A = val;
}

void firefly0(uint8_t val) {
	OCR0B = val;
}

// Enable watchdog interrupt and set prescaling
void setupWDT(uint8_t wdp) {
	cli();
	// Start timed sequence
	// Set Watchdog Change Enable bit
	WDTCR |= (1<<WDCE);
	// Set new prescaler, unset reset enable
	// enable WDT interrupt
	WDTCR = (1<<WDTIE) | wdp;
}

void sleep(uint8_t s, uint8_t mode) {
	sei();
	sleep_interval = 0;
	while (sleep_interval < s) {
		set_sleep_mode(mode);
		wdt_reset();
		sleep_mode();
	}
	cli();
}

void adcEnable() {
	PRR &= ~(1<<PRADC);
	ADMUX =
		(0 << REFS0) |             // VCC as voltage reference
		(0 << ADLAR) |             // 10 Bit resolution
		(1 << MUX1)  |             // ADC2(PB4)
		(0 << MUX0);               // ADC2(PB4)

	ADCSRA =
		(1 << ADEN)  |             // ADC enable
		(0 << ADPS2) |             // set prescaler to 64, bit 2
		(1 << ADPS1) |             // set prescaler to 64, bit 1
		(1 << ADPS0);              // set prescaler to 64, bit 0
}

void adcDisable() {
	ADCSRA &= ~(1<<ADEN);          // remove ADC enable
	PRR |= (1<<PRADC);
}

uint16_t readADC() {
	ADCSRA |= (1 << ADSC);         // start ADC measurement.
	while (ADCSRA & (1 << ADSC) ); // wait till conversion completes

	return ADCW;
}

uint16_t readLightLevel() {
	uint16_t lightLevel;

	adcEnable();
	readADC();                     // dummy read
	lightLevel = readADC();        // reading ADC

	adcDisable();
	return lightLevel;
}

int main() {
	uint8_t i;
	uint16_t lightLevel;
	uint16_t pattern;

	uint8_t offset0, intensity_shift0;
	uint8_t offset1, intensity_shift1;

	// We will never use the AC
	ACSR |= (1<<ACD);
	// We do not have any digital input
	DIDR0 = (1<<ADC0D) | (1<<ADC2D) | (1<<ADC3D) | (1<<ADC1D) | (1<<AIN1D) | (1<<AIN0D);

	// Setup OC0B (PB1) and OC0A (PB0) as output
	DDRB = (1<<PB1) | (1<<PB0);

	// Setup PWM channels and Watchdog
	setupPWM();
	setupWDT(WDP_8S);

	darkness = LOWER_LIMIT;
	firefly_tokens = TOKENS_MAX;

	while(1) {
		lightLevel = readLightLevel();

		if(lightLevel > LIGTH_THRESHOLD) {
			if(darkness < UPPER_LIMIT) darkness++;
		} else {
			if(darkness > LOWER_LIMIT) darkness--;
		}

		if(darkness >= CONFIDENCE_LIMIT) {
			pattern = random() ^ lightLevel;
			if(firefly_tokens > 0) {
				firefly_tokens--;

				// Pull PB4 to GND for PWM
				DDRB |= (1<<PB4);
				// Enable PWM channel
				TCCR0A |= (1 << COM0B1);
				TCCR0A |= (1 << COM0A1);

				offset0 = 0;
				offset1 = 0;
				intensity_shift0 = 0;
				intensity_shift1 = 0;
				if(pattern & FIREFLY0_OFFSET)
					offset0 = 2;
				if(pattern & FIREFLY1_OFFSET)
					offset1 = 2;

				if(pattern & FIREFLY0_INTENSITY)
					intensity_shift0 = 1;
				if(pattern & FIREFLY1_INTENSITY)
					intensity_shift1 = 1;

				// firefly sequence
				setupWDT(WDP_32MS);
				for(i=0; i < (SEQ_SIZE - 2); i++) {
					if(pattern & FIREFLY0_ENABLED)
						firefly0(pgm_read_byte(&sequence[i+offset0])>>intensity_shift0);
					if(pattern & FIREFLY1_ENABLED)
						firefly1(pgm_read_byte(&sequence[i+offset1])>>intensity_shift1);
					sleep(1, SLEEP_MODE_IDLE);
				}

				// Disable PWM channels
				TCCR0A &= ~(1 << COM0B1);
				TCCR0A &= ~(1 << COM0A1);
				// Recover PB4 for ADC read
				DDRB &= ~(1<<PB4);

				// Recover WDT after changing WDT to smaller value
				setupWDT(WDP_8S);
			}
		} else {
			if(firefly_tokens < TOKENS_MAX) {
				firefly_tokens++;
			}
		}

		// pause between firefly action
		if((pattern & FIREFLY0_ENABLED) && (pattern & FIREFLY1_ENABLED))       // Both fireflies in action
			sleep(2, SLEEP_MODE_PWR_DOWN);
		else if((pattern & FIREFLY0_ENABLED) || (pattern & FIREFLY1_ENABLED))  // One of two in action
			sleep(1, SLEEP_MODE_PWR_DOWN);
	}
}

ISR(WDT_vect) {
	sleep_interval++;
	wdt_reset();
	// Re-enable WDT interrupt. Normally we wouldn't do that here,
	// But we're using this routine purely as a timeout;
	// WDT is never used for reset
	WDTCR |= (1<<WDTIE);
}
