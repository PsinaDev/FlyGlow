#pragma once

#include <QColor>
#include <QIcon>

namespace fg::icons {

enum class Id {
    Play, Pause, Plus, Minus, Trash, Duplicate, Up, Down, Eye, EyeOff,
    Brush, Eraser, Fill, Picker, Mask, Undo, Redo, Open, Save, New,
    Send, Settings, KeyAdd, KeyRemove, Clear, Dock, Film,
};

QIcon get(Id id, const QColor& color = QColor(0xe6, 0xe8, 0xee));

} // namespace fg::icons
