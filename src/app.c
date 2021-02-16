/******************************************************************************
 
 Copyright (c) 2015, Focusrite Audio Engineering Ltd.
 All rights reserved.
 
 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:
 
 * Redistributions of source code must retain the above copyright notice, this
 list of conditions and the following disclaimer.
 
 * Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following disclaimer in the documentation
 and/or other materials provided with the distribution.
 
 * Neither the name of Focusrite Audio Engineering Ltd., nor the names of its
 contributors may be used to endorse or promote products derived from
 this software without specific prior written permission.
 
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 
 *****************************************************************************/

//______________________________________________________________________________
//
// Headers
//______________________________________________________________________________

#include "app.h"
#include "step.h"
#include <stdio.h>
#include <stdlib.h>
//#include <time.h>
#include <stdbool.h>


/***** global variables *****/
static const u16 *g_ADC = 0;   // ADC frame pointer
static u8 g_Buttons[BUTTON_COUNT] = {0};
static u8 clock = INTERNAL;
static Stage stages[STAGE_COUNT];
static Color palette[PSIZE];
const u8 note_map[8] = { 0, 2, 4, 5, 7, 9, 11, 12 };  // maps the major scale to note intervals

static u8 warning_level = 0;
static u8 warning_blink = 0;

static bool is_playing = false;
static u8 current_marker = OFF_MARKER;
static u8 current_note = OUT_OF_RANGE;


/***** helper functions *****/

#define send_midi(s, d1, d2)    (hal_send_midi(lp_midi_port, (s), (d1), (d2)))

void plot_led(u8 type, u8 index, Color color) {
	hal_plot_led(type, index, color.red, color.green, color.blue);
}

void warning(u8 level) {
	Color color = palette[BLACK];
	if (level < PSIZE) {
		color = palette[level];
	}
    hal_plot_led(TYPESETUP, 0, color.red, color.green, color.blue);
    warning_level = level;
    warning_blink = 0;
}

#define DEBUG true
void debug(u8 index, u8 level) {
	if (DEBUG && level != OUT_OF_RANGE) {
		plot_led(TYPEPAD, (index + 1) * 10, palette[level]);
	}
}

/***** pads (the central grid) *****/

u8 pad_index(u8 row, u8 column) {
	return (row + 1) * 10 + column + 1;
}

// calculate row from pad index
// the bottom row is actually the buttons, so check for that and shift by 1
u8 index_to_row(u8 index) {
	u8 row = index / 10;
	if (row < 1 || row > ROW_COUNT) {
		return OUT_OF_RANGE;
	}
	return row - 1;
}

// calculate column from pad index
// 0 and 9 are the left and right buttons
u8 index_to_column(u8 index) {
	u8 column = index % 10;
	if (column < 1 || column > COLUMN_COUNT) {
		return OUT_OF_RANGE;
	}
	return column - 1;
}

bool is_pad(u8 row, u8 column) {
	return row != OUT_OF_RANGE && column != OUT_OF_RANGE;
}

void set_pad(u8 row, u8 column, u8 value) {
	if (is_pad(row, column)) {
		u8 index = pad_index(row, column);
		g_Buttons[index] = value;
		plot_led(TYPEPAD, index, palette[value]);
	}
}

u8 get_pad(u8 row, u8 column) {
	if (is_pad(row, column)) {
		u8 index = pad_index(row, column);
		return g_Buttons[index];
	}
	return OUT_OF_RANGE;
}


/***** buttons (the buttons around the sides) *****/

u8 button_index(u8 group, u8 offset) {
	if (group == TOP) {
		return 91 + offset;
	}
	if (group == BOTTOM) {
		return 1 + offset;
	}
	if (group == LEFT) {
		return (offset + 1) * 10;
	}
	if (group == RIGHT) {
		return (offset + 1) * 10 + 9;
	}
	return OUT_OF_RANGE;
}

u8 index_to_group(u8 index) {
	u8 r = index / 10;
	u8 c = index % 10;
	if (r == 0) {
		return BOTTOM;
	}
	if (r == 9) {
		return TOP;
	}
	if (c == 0) {
		return LEFT;
	}
	if (c == 9) {
		return RIGHT;
	}
	return OUT_OF_RANGE;
}

u8 index_to_offset(u8 index) {
	u8 group = index_to_group(index);

	if (group == TOP || group == BOTTOM) {
		return (index % 10) - 1;
	}
	if (group == LEFT || group == RIGHT) {
		return (index / 10) - 1;
	}
	return OUT_OF_RANGE;
}

bool is_button(u8 group, u8 offset) {
	return group != OUT_OF_RANGE && offset != OUT_OF_RANGE;
}

void set_button(u8 group, u8 offset, u8 value) {
	if (is_button(group, offset)) {
		u8 index = button_index(group, offset);
		g_Buttons[index] = value;
		plot_led(TYPEPAD, index, palette[value]);
	}
}

u8 get_button(u8 group, u8 offset) {
	if (is_button(group, offset)) {
		u8 index = button_index(group, offset);
		return g_Buttons[index];
	}
	return OUT_OF_RANGE;
}


/***** draw *****/

void draw_markers() {
	set_button(MARKER_GROUP, 0, OFF_MARKER);
	set_button(MARKER_GROUP, 1, NOTE_MARKER);
	set_button(MARKER_GROUP, 2, SHARP_MARKER);
	set_button(MARKER_GROUP, 3, OCTAVE_UP_MARKER);
	set_button(MARKER_GROUP, 4, VELOCITY_UP_MARKER);
	set_button(MARKER_GROUP, 5, EXTEND_MARKER);
	set_button(MARKER_GROUP, 6, TIE_MARKER);
	set_button(MARKER_GROUP, 7, LEGATO_MARKER);
	current_marker = NOTE_MARKER;
	set_button(RIGHT, 0, current_marker);
}


/***** stages *****/

u8 get_note(Stage stage) {
	if (stage.note == OUT_OF_RANGE) {
		return OUT_OF_RANGE;
	}
	return (stage.octave + DEFAULT_OCTAVE) * 12 + note_map[stage.note] + stage.accidental;
}

u8 get_velocity(Stage stage) {
	s8 v = DEFAULT_VELOCITY + stage.velocity * VELOCITY_DELTA;
	v = v < 1 ? 1 : (v > 127 ? 127 : v);
	return v;
}

void note_off() {
	if (current_note != OUT_OF_RANGE) {
		hal_send_midi(USBMIDI, NOTEOFF | 0, current_note, 0);
		current_note = OUT_OF_RANGE;
	}
}

// update_stage updates stage settings when a marker is changed.
//   if turn_on=true, a marker was added; if false, a marker was removed.
//   for some markers (NOTE), row placement matters.
void update_stage(u8 row, u8 column, u8 marker, bool turn_on) {

	s8 inc = turn_on ? 1 : -1;

	switch (marker) {
		case NOTE_MARKER:
			stages[column].note_count += inc;
			stages[column].note = row;
			break;

		case SHARP_MARKER:
			stages[column].note += inc;
			break;

		case FLAT_MARKER:
			stages[column].note -= inc;
			break;

		case OCTAVE_UP_MARKER:
			stages[column].octave += inc;
			break;

		case OCTAVE_DOWN_MARKER:
			stages[column].octave -= inc;
			break;

		case VELOCITY_UP_MARKER:
			stages[column].velocity += inc;
			break;

		case VELOCITY_DOWN_MARKER:
			stages[column].velocity -= inc;
			break;

		case EXTEND_MARKER:
			stages[column].extend += inc;
			break;

		case REPEAT_MARKER:
			stages[column].repeat += inc;
			break;

		case TIE_MARKER:
			stages[column].tie += inc;
			break;

		case LEGATO_MARKER:
			stages[column].legato += inc;
			break;
	}

}


/***** handlers *****/

void on_pad(u8 index, u8 row, u8 column, u8 value) {

	if (value) {
		u8 previous = get_pad(row, column);
		bool turn_on = (previous != current_marker);

		// remove the old marker that was at this row
		update_stage(row, column, previous, false);

		if (turn_on) {
			set_pad(row, column, current_marker);
			// add the new marker
			update_stage(row, column, current_marker, true);
		} else {
			set_pad(row, column, OFF_MARKER);
		}

	}
}

void on_button(u8 index, u8 group, u8 offset, u8 value) {
	if (index == PLAY_BUTTON) {
		is_playing = !is_playing;
		set_button(group, offset, is_playing ? WHITE : BLACK);

	} else if (group == MARKER_GROUP) {

		u8 m = get_button(group, offset);
		u8 n = OUT_OF_RANGE;

		// some markers require clever fooferaw
		switch (m) {
			case SHARP_MARKER:
				if (current_marker == SHARP_MARKER) {
					n = FLAT_MARKER;
				} else {
					n = SHARP_MARKER;
				}
				break;
			case OCTAVE_UP_MARKER:
				if (current_marker == OCTAVE_UP_MARKER) {
					n = OCTAVE_DOWN_MARKER;
				} else {
					n = OCTAVE_UP_MARKER;
				}
				break;
			case VELOCITY_UP_MARKER:
				if (current_marker == VELOCITY_UP_MARKER) {
					n = VELOCITY_DOWN_MARKER;
				} else {
					n = VELOCITY_UP_MARKER;
				}
				break;
			default:
				n = m;
				break;
		}

		// display current marker
		if (n != OUT_OF_RANGE) {
			plot_led(TYPEPAD, DISPLAY_BUTTON, palette[n]);
			current_marker = n;
		}

	} else {
		set_button(group, offset, BLACK);
	}
}


/***** timing *****/

// pulse is called for every MIDI (or internal) pulse, 96 times per quarter note.
void pulse() {

	static int pulse_count = 0;

	if (!is_playing) {
		note_off();
		return;
	}

    int last = (pulse_count + 7) % 8;
    plot_led(TYPEPAD, 91 + last, palette[BLACK]);
    plot_led(TYPEPAD, 91 + pulse_count, palette[RED]);

    Stage current = stages[pulse_count];

    // if it's a tie, do nothing
    // if it's legato, send previous note off after new note on
    // otherwise, send previous note off first
	if (current.tie <= 0) {
		if (current.legato <= 0) {
			note_off();
		}

		u8 previous_note = current_note;
		if (current.note_count > 0 && current.note != OUT_OF_RANGE) {
			u8 n = get_note(current);
			hal_send_midi(USBMIDI, NOTEON | 0, n, get_velocity(current));
			current_note = n;
		}

		if (current.legato > 0) {
			if (previous_note != OUT_OF_RANGE) {
				hal_send_midi(USBMIDI, NOTEOFF | 0, previous_note, 0);
			}
		}
	}

	debug(1, current.note_count > 0 ? NOTE_MARKER : OFF_MARKER);
	debug(2, current.octave > 0 ? OCTAVE_UP_MARKER : OFF_MARKER);
	debug(2, current.octave < 0 ? OCTAVE_DOWN_MARKER : OUT_OF_RANGE);
	debug(3, current.velocity > 0 ? VELOCITY_UP_MARKER : OFF_MARKER);
	debug(3, current.velocity < 0 ? VELOCITY_DOWN_MARKER : OUT_OF_RANGE);
	debug(4, current.legato > 0 ? LEGATO_MARKER : OFF_MARKER);
	debug(4, current.tie > 0 ? TIE_MARKER : OUT_OF_RANGE);

	pulse_count = (pulse_count + 1) % 8;
}

void blink() {

	static int blink = 0;

	if (warning_level > 0) {
		Color color = palette[BLACK];
		if (warning_blink == 0) {
			color = palette[warning_level];
		}
		hal_plot_led(TYPESETUP, 0, color.red, color.green, color.blue);
	}

	blink = 255 - blink;
}


/***** callbacks for the HAL *****/

void app_surface_event(u8 type, u8 index, u8 value)
{
    switch (type)
    {
        case  TYPEPAD:
        {

        	if (value) {
        		u8 row = index_to_row(index);
        		u8 column = index_to_column(index);
        		if (is_pad(row, column)) {
        			on_pad(index, row, column, value);
        		} else {
        			u8 group = index_to_group(index);
        			u8 offset = index_to_offset(index);
        			if (is_button(group, offset)) {
        				on_button(index, group, offset, value);
        			}

        		}
        	}

//            hal_send_midi(USBMIDI, NOTEON | 0, index, value);


//            // toggle it and store it off, so we can save to flash if we want to
//            if (value)
//            {
//                g_Buttons[index] = MAXLED * !g_Buttons[index];
//            }
//
//            // example - light / extinguish pad LEDs
//            hal_plot_led(TYPEPAD, index, 0, 0, g_Buttons[index]);
//
//            // example - send MIDI
//            hal_send_midi(USBMIDI, NOTEON | 0, index, value);
////            hal_send_midi(DINMIDI, NOTEON | 0, index, value);
            
        }
        break;
            
        case TYPESETUP:
        {
            if (value)
            {
                // save button states to flash (reload them by power cycling the hardware!)
                hal_write_flash(0, g_Buttons, BUTTON_COUNT);
            }
        }
        break;
    }
}

//______________________________________________________________________________

void app_midi_event(u8 port, u8 status, u8 d1, u8 d2)
{
    // example - MIDI interface functionality for USB "MIDI" port -> DIN port
    if (port == USBMIDI)
    {
        hal_send_midi(DINMIDI, status, d1, d2);
    }
    
    // // example -MIDI interface functionality for DIN -> USB "MIDI" port port
    if (port == DINMIDI)
    {
        hal_send_midi(USBMIDI, status, d1, d2);
    }
}

//______________________________________________________________________________

void app_sysex_event(u8 port, u8 * data, u16 count)
{
    // example - respond to UDI messages?
}

//______________________________________________________________________________

void app_aftertouch_event(u8 index, u8 value)
{
    // example - send poly aftertouch to MIDI ports
//    hal_send_midi(USBMIDI, POLYAFTERTOUCH | 0, index, value);
//    hal_send_midi(USBMIDI, CHANNELAFTERTOUCH | 0, value, 0);
    
    
}

//______________________________________________________________________________

void app_cable_event(u8 type, u8 value)
{
    // example - light the Setup LED to indicate cable connections
    if (type == MIDI_IN_CABLE)
    {
        hal_plot_led(TYPESETUP, 0, 0, value, 0); // green
    }
    else if (type == MIDI_OUT_CABLE)
    {
        hal_plot_led(TYPESETUP, 0, value, 0, 0); // red
    }
}

//______________________________________________________________________________


void app_timer_event()
{
	#define PULSE_INTERVAL 250
	#define BLINK_INTERVAL 50

    static int tick_count = 0;
	static int pulse_timer = PULSE_INTERVAL;
	static int blink_timer = BLINK_INTERVAL;
    
    if (clock == INTERNAL && pulse_timer >= PULSE_INTERVAL) {
    	pulse_timer = 0;
    	pulse();
    } else {
    	pulse_timer++;
    }
    
    if (blink_timer >= BLINK_INTERVAL) {
    	blink_timer = 0;
    	blink();
    } else {
    	blink_timer++;
    }

    tick_count++;

	// alternative example - show raw ADC data as LEDs
//	for (int i=0; i < PAD_COUNT; ++i)
//	{
//		// raw adc values are 12 bit, but LEDs are 6 bit.
//		// Let's saturate into r;g;b for a rainbow effect to show pressure
//		u16 r = 0;
//		u16 g = 0;
//		u16 b = 0;
//
//		u16 x = (3 * MAXLED * g_ADC[i]) >> 12;
//
//		if (x < MAXLED)
//		{
//			r = x;
//		}
//		else if (x >= MAXLED && x < (2*MAXLED))
//		{
//			r = 2*MAXLED - x;
//			g = x - MAXLED;
//		}
//		else
//		{
//			g = 3*MAXLED - x;
//			b = x - 2*MAXLED;
//		}
//
//		hal_plot_led(TYPEPAD, ADC_MAP[i], r, g, b);
//	}
}


/***** initialization *****/

void set_colors() {

	palette[BLACK] = (Color){0, 0, 0};
	palette[DARK_GRAY] = (Color){C_LO, C_LO, C_LO};
	palette[WHITE] = (Color){C_HI, C_HI, C_HI};
	palette[GRAY] = (Color){C_MID, C_MID, C_MID};

	palette[RED] = (Color){C_HI, 0, 0};
	palette[ORANGE] = (Color){63, 10, 0};
	palette[YELLOW] = (Color){C_HI, C_HI, 0};
	palette[GREEN] = (Color){0, C_HI, 0};
	palette[CYAN] = (Color){0, C_HI, C_HI};
	palette[BLUE] = (Color){0, 0, C_HI};
	palette[PURPLE] = (Color){10, 0, 63};
	palette[MAGENTA] = (Color){C_HI, 0, C_HI};

	palette[DIM_RED] = (Color){C_MID, 0, 0};
	palette[DIM_ORANGE] = (Color){20, 4, 0};
	palette[DIM_YELLOW] = (Color){C_MID, C_MID, 0};
	palette[DIM_GREEN] = (Color){0, C_MID, 0};
	palette[DIM_CYAN] = (Color){0, C_MID, C_MID};
	palette[DIM_BLUE] = (Color){0, 0, C_MID};
	palette[DIM_PURPLE] = (Color){4, 0, 20};
	palette[DIM_MAGENTA] = (Color){C_MID, 0, C_MID};

	palette[SKY_BLUE] = (Color){8, 18, 63};
	palette[PINK] = (Color){63, 16, 16};
	palette[DIM_PINK] = (Color){20, 6, 6};

}

void app_init(const u16 *adc_raw)
{

	set_colors();
	draw_markers();

	for (int s = 0; s < 8; s++) {
		stages[s] = (Stage) { 0, OUT_OF_RANGE, 0, 0, 0, 0, 0, 0, 0 };
	}

	warning(SKY_BLUE);

//	time_t t;
//	srand((unsigned) time(&t));

    // example - load button states from flash
//    hal_read_flash(0, g_Buttons, BUTTON_COUNT);
    
    // example - light the LEDs to say hello!
//    for (int i=0; i < 10; ++i)
//    {
//        for (int j=0; j < 10; ++j)
//        {
////            u8 b = g_Buttons[j*10 + i];
////            hal_plot_led(TYPEPAD, j*10 + i, b, b, b);
//        	u8 index = j*10 + i;
//        	if (g_Buttons[index] < 0 || g_Buttons[index] >= PSIZE) {
//        		g_Buttons[index] = 0;
//        	}
//            plot_led(TYPEPAD, index, palette[g_Buttons[index]]);
//        }
//    }

	// store off the raw ADC frame pointer for later use
	g_ADC = adc_raw;
}
