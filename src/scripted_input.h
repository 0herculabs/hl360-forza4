#pragma once

namespace rex::input {
class InputSystem;
}

namespace fm4 {
// Adds the virtual test pad when input_script is set (scripted_input.cpp).
void AddScriptedInput(rex::input::InputSystem& input);
}
