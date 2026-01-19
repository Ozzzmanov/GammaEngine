#pragma once
#include <windows.h>
#include <iostream>

inline std::ostream& BLUE(std::ostream& s) {
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    return s;
}

inline std::ostream& RED(std::ostream& s) {
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED | FOREGROUND_INTENSITY);
    return s;
}

inline std::ostream& GREEN(std::ostream& s) {
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    return s;
}

inline std::ostream& YELLOW(std::ostream& s) {
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    return s;
}

inline std::ostream& WHITE(std::ostream& s) {
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    return s;
}