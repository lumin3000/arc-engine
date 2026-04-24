
#include "input.h"
#include "../../../external/sokol/c/sokol_app.h"
#include <string.h>
#include <stdio.h>

Input* input_state = NULL;
Input _actual_input_state = {0};

void (*window_resize_callback)(int width, int height) = NULL;

bool key_pressed(Key_Code code) {
    return (input_state->keys[code] & INPUT_FLAG_PRESSED) != 0;
}

bool key_released(Key_Code code) {
    return (input_state->keys[code] & INPUT_FLAG_RELEASED) != 0;
}

bool key_down(Key_Code code) {
    return (input_state->keys[code] & INPUT_FLAG_DOWN) != 0;
}

bool key_repeat(Key_Code code) {
    return (input_state->keys[code] & INPUT_FLAG_REPEAT) != 0;
}

void consume_key_pressed(Key_Code code) {
    input_state->keys[code] &= ~INPUT_FLAG_PRESSED;
}

void consume_key_released(Key_Code code) {
    input_state->keys[code] &= ~INPUT_FLAG_RELEASED;
}

bool any_key_press_and_consume(void) {
    for (int key = 0; key < MAX_KEYCODES; key++) {

        if (key >= KEY_LEFT_MOUSE) continue;

        if (input_state->keys[key] & INPUT_FLAG_PRESSED) {
            input_state->keys[key] &= ~INPUT_FLAG_PRESSED;
            return true;
        }
    }
    return false;
}

void reset_input_state(Input* input) {

    for (int i = 0; i < MAX_KEYCODES; i++) {
        input->keys[i] &= INPUT_FLAG_DOWN;
    }

    input->scroll_x = 0.0f;
    input->scroll_y = 0.0f;

    input->text_input[0] = '\0';
    input->text_input_len = 0;
}

void add_input(Input* dest, const Input* src) {
    dest->mouse_x = src->mouse_x;
    dest->mouse_y = src->mouse_y;
    dest->scroll_x += src->scroll_x;
    dest->scroll_y += src->scroll_y;

    for (int i = 0; i < MAX_KEYCODES; i++) {
        dest->keys[i] |= src->keys[i];
    }
}

static Key_Code map_sokol_mouse_button(sapp_mousebutton button) {
    switch (button) {
        case SAPP_MOUSEBUTTON_LEFT:   return KEY_LEFT_MOUSE;
        case SAPP_MOUSEBUTTON_RIGHT:  return KEY_RIGHT_MOUSE;
        case SAPP_MOUSEBUTTON_MIDDLE: return KEY_MIDDLE_MOUSE;
        default: return KEY_INVALID;
    }
}

void event_callback(const sapp_event* event) {

    if (event->type == SAPP_EVENTTYPE_CLIPBOARD_PASTED) {
        fprintf(stderr, "[input] CLIPBOARD_PASTED received, content='%.50s'\n", sapp_get_clipboard_string());
    }

    extern bool simgui_handle_event_wrapper(const sapp_event *);
    simgui_handle_event_wrapper(event);

    Input* input = &_actual_input_state;

    switch (event->type) {
        case SAPP_EVENTTYPE_RESIZED:
            if (window_resize_callback) {
                window_resize_callback(event->window_width, event->window_height);
            } else {
                fprintf(stderr, "WARNING: no window_resize_callback defined\n");
            }
            break;

        case SAPP_EVENTTYPE_MOUSE_SCROLL:
            input->scroll_x = event->scroll_x;
            input->scroll_y = event->scroll_y;
            break;

        case SAPP_EVENTTYPE_MOUSE_MOVE:
            input->mouse_x = event->mouse_x;
            input->mouse_y = event->mouse_y;
            break;

        case SAPP_EVENTTYPE_MOUSE_UP: {
            Key_Code key = map_sokol_mouse_button(event->mouse_button);
            if (key != KEY_INVALID && (input->keys[key] & INPUT_FLAG_DOWN)) {
                input->keys[key] &= ~INPUT_FLAG_DOWN;
                input->keys[key] |= INPUT_FLAG_RELEASED;
            }
            break;
        }

        case SAPP_EVENTTYPE_MOUSE_DOWN: {
            Key_Code key = map_sokol_mouse_button(event->mouse_button);
            if (key != KEY_INVALID && !(input->keys[key] & INPUT_FLAG_DOWN)) {
                input->keys[key] |= (INPUT_FLAG_DOWN | INPUT_FLAG_PRESSED);
            }
            break;
        }

        case SAPP_EVENTTYPE_KEY_UP:
            if (event->key_code < MAX_KEYCODES &&
                (input->keys[event->key_code] & INPUT_FLAG_DOWN)) {
                input->keys[event->key_code] &= ~INPUT_FLAG_DOWN;
                input->keys[event->key_code] |= INPUT_FLAG_RELEASED;
            }
            break;

        case SAPP_EVENTTYPE_KEY_DOWN:
            if (event->key_code < MAX_KEYCODES) {
                if (!event->key_repeat && !(input->keys[event->key_code] & INPUT_FLAG_DOWN)) {
                    input->keys[event->key_code] |= (INPUT_FLAG_DOWN | INPUT_FLAG_PRESSED);
                }
                if (event->key_repeat) {
                    input->keys[event->key_code] |= INPUT_FLAG_REPEAT;
                }
            }
            break;

        case SAPP_EVENTTYPE_CHAR:

            if (event->char_code != 127 && event->char_code < 256) {
                if (input->text_input_len < TEXT_INPUT_BUFFER_SIZE - 1) {
                    input->text_input[input->text_input_len++] = (char)(event->char_code & 255);
                    input->text_input[input->text_input_len] = '\0';
                }
            }
            break;

        default:
            break;
    }
}
