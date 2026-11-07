// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <memory>
#include <windows.h>

class BackdropHost {
public:
 BackdropHost();
 ~BackdropHost();
 bool update(HWND window,int amount);
 void remove(HWND window);

private:
 struct State;
 std::unique_ptr<State> state_;
};
