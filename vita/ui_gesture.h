#pragma once
/* No model/UI dependencies: gesture events are based on valid panel samples. */
typedef enum { UI_NONE, UI_COMPOSE, UI_SEND, UI_STOP, UI_NEW, UI_MENU, UI_THEME, UI_LATEST, UI_SCROLL } UiAction;
typedef struct { int active, moved, start_x, start_y, last_y; UiAction action; } UiGesture;
typedef struct { UiAction action; int scroll; } UiGestureEvent;
UiGestureEvent ui_gesture_step(UiGesture*,int down,int x,int y,UiAction hit);
void ui_gesture_cancel(UiGesture*);
