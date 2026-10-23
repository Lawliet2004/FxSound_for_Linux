#include "windows.h"
#include <cassert>
#include <clocale>
#include <unistd.h>
int main() {
    setlocale(LC_ALL, "C");
    char temporary[] = "/tmp/fxsound-wfopen-XXXXXX";
    char* directory = mkdtemp(temporary); assert(directory);
    assert(chdir(directory) == 0);
    FILE* file = nullptr;
    assert(_wfopen_s(&file, L"音楽é😀.fac", L"wb") == 0);
    assert(fwrite("preset", 1, 6, file) == 6); fclose(file);
    file = fopen("音楽é😀.fac", "rb"); assert(file); fclose(file);
    assert(_wfopen_s(&file, L"missing.fac", L"rb") == ENOENT && !file);
    assert(_wfopen_s(&file, nullptr, L"rb") == EINVAL && !file);
    wchar_t invalid[] = {0xd800, 0};
    assert(_wfopen_s(&file, invalid, L"rb") == EILSEQ && !file);
    assert(_wfopen_s(&file, L"file", L"rrrrrrrrrrrrrrrrrrrrrrrr") == EINVAL);
    unlink("音楽é😀.fac"); chdir("/"); rmdir(directory);
}
