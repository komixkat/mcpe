#pragma once

inline bool (*game_window_is_mouse_locked)(void* window);
inline void (*game_window_add_window_creation_callback)(void* user, void (*callback)(void* user));
inline void* (*game_window_get_primary_window)();
inline void (*game_window_add_keyboard_callback)(void* window, void* user, bool (*callback)(void* user, int keyCode, int action));

inline int (*VanillaCameraAPI_getPlayerViewPerspectiveOption_orig)(void*);