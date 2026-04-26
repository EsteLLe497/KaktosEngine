#include "framework.h"
#include "Scenario.h"

#include <algorithm>
#include <cwctype>
#include <sstream>

namespace
{
    std::wstring GetAttributeValue(const std::wstring& source, const std::wstring& key)
    {
        const std::wstring pattern = key + L"=\"";
        const size_t start = source.find(pattern);
        if (start == std::wstring::npos)
        {
            return L"";
        }

        const size_t valueStart = start + pattern.size();
        const size_t valueEnd = source.find(L'"', valueStart);
        if (valueEnd == std::wstring::npos)
        {
            return L"";
        }

        return source.substr(valueStart, valueEnd - valueStart);
    }

    std::wstring GetTagName(const std::wstring& source)
    {
        const size_t delimiter = source.find_first_of(L" \t");
        return delimiter == std::wstring::npos ? source : source.substr(0, delimiter);
    }

    bool IsTagLine(const std::wstring& text)
    {
        return text.size() >= 2 && text.front() == L'[' && text.back() == L']';
    }

    ScriptCommand MakeCommand(ScriptCommand::Type type, size_t sourceLine)
    {
        ScriptCommand command;
        command.type = type;
        command.sourceLine = sourceLine;
        command.id = L"cmd_" + std::to_wstring(sourceLine);
        return command;
    }

    void SetCommandParameter(ScriptCommand& command, const std::wstring& key, const std::wstring& value)
    {
        command.parameters[key] = value;
    }

    std::string WideToUtf8(const std::wstring& text)
    {
        if (text.empty())
        {
            return std::string{};
        }

        const int requiredBytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (requiredBytes <= 0)
        {
            return std::string{};
        }

        std::string utf8(requiredBytes, '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &utf8[0], requiredBytes, nullptr, nullptr);
        return utf8;
    }

    std::wstring EscapeAttribute(const std::wstring& value)
    {
        std::wstring escaped = value;
        std::replace(escaped.begin(), escaped.end(), L'"', L'\'');
        std::replace(escaped.begin(), escaped.end(), L'\r', L' ');
        std::replace(escaped.begin(), escaped.end(), L'\n', L' ');
        return escaped;
    }

    std::wstring AppendAttribute(const std::wstring& key, const std::wstring& value)
    {
        if (value.empty())
        {
            return L"";
        }
        return L" " + key + L"=\"" + EscapeAttribute(value) + L"\"";
    }
}

std::wstring Trim(const std::wstring& text)
{
    const auto begin = std::find_if_not(text.begin(), text.end(), iswspace);
    if (begin == text.end())
    {
        return L"";
    }

    const auto end = std::find_if_not(text.rbegin(), text.rend(), iswspace).base();
    return std::wstring(begin, end);
}

std::wstring Utf8ToWide(const std::string& text)
{
    if (text.empty())
    {
        return L"";
    }

    const int requiredChars = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    if (requiredChars <= 0)
    {
        return L"";
    }

    std::wstring wideText(requiredChars, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &wideText[0], requiredChars);
    return wideText;
}

std::wstring GetDirectoryPath(const std::wstring& path)
{
    const size_t lastSeparator = path.find_last_of(L"\\/");
    if (lastSeparator == std::wstring::npos)
    {
        return L".";
    }

    return path.substr(0, lastSeparator);
}

std::wstring CombinePath(const std::wstring& baseDir, const std::wstring& relativePath)
{
    if (relativePath.empty())
    {
        return baseDir;
    }

    if (relativePath.size() >= 2 && relativePath[1] == L':')
    {
        return relativePath;
    }

    if (relativePath.front() == L'\\' || relativePath.front() == L'/')
    {
        return relativePath;
    }

    if (baseDir.empty() || baseDir == L".")
    {
        return relativePath;
    }

    return baseDir + L"\\" + relativePath;
}

std::vector<std::wstring> GetScenarioCandidates()
{
    std::vector<std::wstring> candidates;

    WCHAR modulePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) > 0)
    {
        std::wstring executablePath = modulePath;
        const size_t lastSeparator = executablePath.find_last_of(L"\\/");
        const std::wstring executableDir = lastSeparator == std::wstring::npos ? L"." : executablePath.substr(0, lastSeparator);

        candidates.push_back(executableDir + L"\\assets\\main.ks");
        candidates.push_back(executableDir + L"\\..\\assets\\main.ks");
        candidates.push_back(executableDir + L"\\..\\..\\assets\\main.ks");
        candidates.push_back(executableDir + L"\\..\\..\\..\\assets\\main.ks");
    }

    candidates.push_back(L"assets\\main.ks");
    candidates.push_back(L"..\\assets\\main.ks");
    candidates.push_back(L"..\\..\\assets\\main.ks");
    return candidates;
}

std::vector<std::wstring> EnumerateFiles(const std::wstring& directory, const std::wstring& pattern)
{
    std::vector<std::wstring> files;
    WIN32_FIND_DATAW data = {};
    HANDLE findHandle = FindFirstFileW((directory + L"\\" + pattern).c_str(), &data);
    if (findHandle == INVALID_HANDLE_VALUE)
    {
        return files;
    }

    do
    {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        {
            files.push_back(directory + L"\\" + data.cFileName);
        }
    } while (FindNextFileW(findHandle, &data));

    FindClose(findHandle);
    return files;
}

std::wstring GetFileNamePart(const std::wstring& path)
{
    const size_t lastSeparator = path.find_last_of(L"\\/");
    return lastSeparator == std::wstring::npos ? path : path.substr(lastSeparator + 1);
}

std::wstring GetFileStemPart(const std::wstring& path)
{
    const std::wstring fileName = GetFileNamePart(path);
    const size_t dot = fileName.find_last_of(L'.');
    return dot == std::wstring::npos ? fileName : fileName.substr(0, dot);
}

std::wstring MakeRelativePath(const std::wstring& fullPath, const std::wstring& baseDir)
{
    if (baseDir.empty())
    {
        return fullPath;
    }

    const std::wstring prefix = baseDir + L"\\";
    if (fullPath.rfind(prefix, 0) == 0)
    {
        return fullPath.substr(prefix.size());
    }
    return fullPath;
}

bool TryReadTextFile(const std::wstring& path, std::wstring& content)
{
    HANDLE fileHandle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (fileHandle == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    LARGE_INTEGER fileSize = {};
    if (!GetFileSizeEx(fileHandle, &fileSize) || fileSize.QuadPart < 0 || fileSize.QuadPart > MAXDWORD)
    {
        CloseHandle(fileHandle);
        return false;
    }

    std::string bytes(static_cast<size_t>(fileSize.QuadPart), '\0');
    DWORD bytesRead = 0;
    const BOOL readOk = bytes.empty() ? TRUE : ReadFile(fileHandle, &bytes[0], static_cast<DWORD>(bytes.size()), &bytesRead, nullptr);
    CloseHandle(fileHandle);

    if (!readOk)
    {
        return false;
    }

    bytes.resize(bytesRead);
    if (bytes.size() >= 3 &&
        static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF)
    {
        bytes.erase(0, 3);
    }

    content = Utf8ToWide(bytes);
    return !content.empty() || bytes.empty();
}

bool TryWriteTextFile(const std::wstring& path, const std::wstring& content)
{
    const std::string utf8 = WideToUtf8(content);
    HANDLE fileHandle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (fileHandle == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
    DWORD written = 0;
    BOOL ok = WriteFile(fileHandle, bom, sizeof(bom), &written, nullptr);
    if (ok && !utf8.empty())
    {
        ok = WriteFile(fileHandle, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    }

    CloseHandle(fileHandle);
    return ok == TRUE;
}

void RebuildScenarioLabels(ScenarioDocument& document)
{
    document.labels.clear();
    for (size_t i = 0; i < document.commands.size(); ++i)
    {
        if (document.commands[i].type == ScriptCommand::Type::Label)
        {
            const auto found = document.commands[i].parameters.find(L"name");
            if (found != document.commands[i].parameters.end() && !found->second.empty())
            {
                document.labels[found->second] = i;
            }
        }
    }
}

std::wstring SerializeScenario(const ScenarioDocument& document)
{
    std::wstring output;
    for (const ScriptCommand& command : document.commands)
    {
        switch (command.type)
        {
        case ScriptCommand::Type::Title:
            output += L"[title" + AppendAttribute(L"name", command.parameters.count(L"name") ? command.parameters.at(L"name") : L"") + L"]\r\n";
            break;
        case ScriptCommand::Type::Background:
            output += L"[bg";
            output += AppendAttribute(L"name", command.parameters.count(L"name") ? command.parameters.at(L"name") : L"");
            output += AppendAttribute(L"storage", command.parameters.count(L"storage") ? command.parameters.at(L"storage") : L"");
            output += AppendAttribute(L"color", command.parameters.count(L"color") ? command.parameters.at(L"color") : L"");
            output += AppendAttribute(L"visible", command.parameters.count(L"visible") ? command.parameters.at(L"visible") : L"");
            output += AppendAttribute(L"x", command.parameters.count(L"x") ? command.parameters.at(L"x") : L"");
            output += AppendAttribute(L"y", command.parameters.count(L"y") ? command.parameters.at(L"y") : L"");
            output += AppendAttribute(L"scale", command.parameters.count(L"scale") ? command.parameters.at(L"scale") : L"");
            output += AppendAttribute(L"opacity", command.parameters.count(L"opacity") ? command.parameters.at(L"opacity") : L"");
            output += L"]\r\n";
            break;
        case ScriptCommand::Type::Character:
            output += L"[ch";
            output += AppendAttribute(L"pos", command.parameters.count(L"pos") ? command.parameters.at(L"pos") : L"");
            output += AppendAttribute(L"name", command.parameters.count(L"name") ? command.parameters.at(L"name") : L"");
            output += AppendAttribute(L"storage", command.parameters.count(L"storage") ? command.parameters.at(L"storage") : L"");
            output += AppendAttribute(L"visible", command.parameters.count(L"visible") ? command.parameters.at(L"visible") : L"");
            output += AppendAttribute(L"x", command.parameters.count(L"x") ? command.parameters.at(L"x") : L"");
            output += AppendAttribute(L"y", command.parameters.count(L"y") ? command.parameters.at(L"y") : L"");
            output += AppendAttribute(L"scale", command.parameters.count(L"scale") ? command.parameters.at(L"scale") : L"");
            output += AppendAttribute(L"opacity", command.parameters.count(L"opacity") ? command.parameters.at(L"opacity") : L"");
            output += AppendAttribute(L"fade", command.parameters.count(L"fade") ? command.parameters.at(L"fade") : L"");
            output += AppendAttribute(L"fade_time", command.parameters.count(L"fade_time") ? command.parameters.at(L"fade_time") : L"");
            output += AppendAttribute(L"fade_wait", command.parameters.count(L"fade_wait") ? command.parameters.at(L"fade_wait") : L"");
            output += L"]\r\n";
            break;
        case ScriptCommand::Type::HideCharacter:
            output += L"[hidech";
            output += AppendAttribute(L"pos", command.parameters.count(L"pos") ? command.parameters.at(L"pos") : L"");
            output += AppendAttribute(L"fade", command.parameters.count(L"fade") ? command.parameters.at(L"fade") : L"");
            output += AppendAttribute(L"fade_time", command.parameters.count(L"fade_time") ? command.parameters.at(L"fade_time") : L"");
            output += AppendAttribute(L"fade_wait", command.parameters.count(L"fade_wait") ? command.parameters.at(L"fade_wait") : L"");
            output += L"]\r\n";
            break;
        case ScriptCommand::Type::Speaker:
            output += L"[speaker" + AppendAttribute(L"name", command.parameters.count(L"name") ? command.parameters.at(L"name") : L"") + L"]\r\n";
            break;
        case ScriptCommand::Type::ClearSpeaker:
            output += L"[clear_speaker]\r\n";
            break;
        case ScriptCommand::Type::Text:
            output += L"[text";
            output += AppendAttribute(L"value", command.parameters.count(L"value") ? command.parameters.at(L"value") : L"");
            output += AppendAttribute(L"name_visible", command.parameters.count(L"name_visible") ? command.parameters.at(L"name_visible") : L"");
            output += AppendAttribute(L"name_target", command.parameters.count(L"name_target") ? command.parameters.at(L"name_target") : L"");
            output += AppendAttribute(L"name_x", command.parameters.count(L"name_x") ? command.parameters.at(L"name_x") : L"");
            output += AppendAttribute(L"name_y", command.parameters.count(L"name_y") ? command.parameters.at(L"name_y") : L"");
            output += AppendAttribute(L"name_image", command.parameters.count(L"name_image") ? command.parameters.at(L"name_image") : L"");
            output += L"]\r\n";
            break;
        case ScriptCommand::Type::Choice:
            output += L"[choice";
            output += AppendAttribute(L"prompt", command.parameters.count(L"prompt") ? command.parameters.at(L"prompt") : L"");
            output += AppendAttribute(L"x", command.parameters.count(L"x") ? command.parameters.at(L"x") : L"");
            output += AppendAttribute(L"y", command.parameters.count(L"y") ? command.parameters.at(L"y") : L"");
            output += L"]\r\n";
            for (const auto& link : command.links)
            {
                output += L"[option" + AppendAttribute(L"text", link.first) + AppendAttribute(L"target", link.second) + L"]\r\n";
            }
            output += L"[endchoice]\r\n";
            break;
        case ScriptCommand::Type::Bgm:
            output += L"[bgm";
            output += AppendAttribute(L"category", command.parameters.count(L"category") ? command.parameters.at(L"category") : L"");
            output += AppendAttribute(L"storage", command.parameters.count(L"storage") ? command.parameters.at(L"storage") : L"");
            output += AppendAttribute(L"volume", command.parameters.count(L"volume") ? command.parameters.at(L"volume") : L"");
            output += AppendAttribute(L"loop", command.parameters.count(L"loop") ? command.parameters.at(L"loop") : L"");
            output += AppendAttribute(L"fadein", command.parameters.count(L"fadein") ? command.parameters.at(L"fadein") : L"");
            output += AppendAttribute(L"fadeout", command.parameters.count(L"fadeout") ? command.parameters.at(L"fadeout") : L"");
            output += L"]\r\n";
            break;
        case ScriptCommand::Type::StopBgm:
            output += L"[stopbgm]\r\n";
            break;
        case ScriptCommand::Type::Se:
            output += L"[se";
            output += AppendAttribute(L"category", command.parameters.count(L"category") ? command.parameters.at(L"category") : L"");
            output += AppendAttribute(L"storage", command.parameters.count(L"storage") ? command.parameters.at(L"storage") : L"");
            output += AppendAttribute(L"volume", command.parameters.count(L"volume") ? command.parameters.at(L"volume") : L"");
            output += AppendAttribute(L"loop", command.parameters.count(L"loop") ? command.parameters.at(L"loop") : L"");
            output += AppendAttribute(L"fadein", command.parameters.count(L"fadein") ? command.parameters.at(L"fadein") : L"");
            output += AppendAttribute(L"fadeout", command.parameters.count(L"fadeout") ? command.parameters.at(L"fadeout") : L"");
            output += L"]\r\n";
            break;
        case ScriptCommand::Type::Voice:
            output += L"[voice";
            output += AppendAttribute(L"category", command.parameters.count(L"category") ? command.parameters.at(L"category") : L"");
            output += AppendAttribute(L"storage", command.parameters.count(L"storage") ? command.parameters.at(L"storage") : L"");
            output += AppendAttribute(L"volume", command.parameters.count(L"volume") ? command.parameters.at(L"volume") : L"");
            output += AppendAttribute(L"loop", command.parameters.count(L"loop") ? command.parameters.at(L"loop") : L"");
            output += AppendAttribute(L"fadein", command.parameters.count(L"fadein") ? command.parameters.at(L"fadein") : L"");
            output += AppendAttribute(L"fadeout", command.parameters.count(L"fadeout") ? command.parameters.at(L"fadeout") : L"");
            output += L"]\r\n";
            break;
        case ScriptCommand::Type::Wait:
            output += L"[wait" + AppendAttribute(L"time", command.parameters.count(L"time") ? command.parameters.at(L"time") : L"") + L"]\r\n";
            break;
        case ScriptCommand::Type::ClearText:
            output += L"[cleartext]\r\n";
            break;
        case ScriptCommand::Type::MessageWindow:
            output += L"[messagewindow" + AppendAttribute(L"visible", command.parameters.count(L"visible") ? command.parameters.at(L"visible") : L"") + L"]\r\n";
            break;
        case ScriptCommand::Type::TextSpeed:
            output += L"[textspeed" + AppendAttribute(L"value", command.parameters.count(L"value") ? command.parameters.at(L"value") : L"") + L"]\r\n";
            break;
        case ScriptCommand::Type::MessageFont:
            output += L"[font" + AppendAttribute(L"face", command.parameters.count(L"face") ? command.parameters.at(L"face") : L"") + L"]\r\n";
            break;
        case ScriptCommand::Type::MessageFontReset:
            output += L"[fontreset]\r\n";
            break;
        case ScriptCommand::Type::MessageStyle:
            output += L"[messageui";
            output += AppendAttribute(L"color", command.parameters.count(L"color") ? command.parameters.at(L"color") : L"");
            output += AppendAttribute(L"border", command.parameters.count(L"border") ? command.parameters.at(L"border") : L"");
            output += AppendAttribute(L"opacity", command.parameters.count(L"opacity") ? command.parameters.at(L"opacity") : L"");
            output += AppendAttribute(L"padding", command.parameters.count(L"padding") ? command.parameters.at(L"padding") : L"");
            output += AppendAttribute(L"image", command.parameters.count(L"image") ? command.parameters.at(L"image") : L"");
            output += L"]\r\n";
            break;
        case ScriptCommand::Type::TextColor:
            output += L"[textcolor" + AppendAttribute(L"color", command.parameters.count(L"color") ? command.parameters.at(L"color") : L"") + L"]\r\n";
            break;
        case ScriptCommand::Type::NameColor:
            output += L"[namecolor" + AppendAttribute(L"color", command.parameters.count(L"color") ? command.parameters.at(L"color") : L"") + L"]\r\n";
            break;
        case ScriptCommand::Type::NameWindow:
            output += L"[namewindow" + AppendAttribute(L"visible", command.parameters.count(L"visible") ? command.parameters.at(L"visible") : L"") + L"]\r\n";
            break;
        case ScriptCommand::Type::VerticalText:
            output += L"[vertical" + AppendAttribute(L"enabled", command.parameters.count(L"enabled") ? command.parameters.at(L"enabled") : L"") + L"]\r\n";
            break;
        case ScriptCommand::Type::PageBreak:
            output += L"[page]\r\n";
            break;
        case ScriptCommand::Type::Shake:
            output += L"[shake" + AppendAttribute(L"time", command.parameters.count(L"time") ? command.parameters.at(L"time") : L"") + AppendAttribute(L"power", command.parameters.count(L"power") ? command.parameters.at(L"power") : L"") + L"]\r\n";
            break;
        case ScriptCommand::Type::Fade:
            output += L"[fade" + AppendAttribute(L"time", command.parameters.count(L"time") ? command.parameters.at(L"time") : L"") + AppendAttribute(L"color", command.parameters.count(L"color") ? command.parameters.at(L"color") : L"") + AppendAttribute(L"opacity", command.parameters.count(L"opacity") ? command.parameters.at(L"opacity") : L"") + AppendAttribute(L"parallel", command.parameters.count(L"parallel") ? command.parameters.at(L"parallel") : L"") + AppendAttribute(L"target", command.parameters.count(L"target") ? command.parameters.at(L"target") : L"") + L"]\r\n";
            break;
        case ScriptCommand::Type::Transition:
            output += L"[transition" + AppendAttribute(L"time", command.parameters.count(L"time") ? command.parameters.at(L"time") : L"") + AppendAttribute(L"style", command.parameters.count(L"style") ? command.parameters.at(L"style") : L"") + AppendAttribute(L"color", command.parameters.count(L"color") ? command.parameters.at(L"color") : L"") + AppendAttribute(L"parallel", command.parameters.count(L"parallel") ? command.parameters.at(L"parallel") : L"") + AppendAttribute(L"target", command.parameters.count(L"target") ? command.parameters.at(L"target") : L"") + L"]\r\n";
            break;
        case ScriptCommand::Type::Zoom:
            output += L"[zoom" + AppendAttribute(L"time", command.parameters.count(L"time") ? command.parameters.at(L"time") : L"") + AppendAttribute(L"scale", command.parameters.count(L"scale") ? command.parameters.at(L"scale") : L"") + AppendAttribute(L"parallel", command.parameters.count(L"parallel") ? command.parameters.at(L"parallel") : L"") + L"]\r\n";
            break;
        case ScriptCommand::Type::Pan:
            output += L"[pan" + AppendAttribute(L"time", command.parameters.count(L"time") ? command.parameters.at(L"time") : L"") + AppendAttribute(L"x", command.parameters.count(L"x") ? command.parameters.at(L"x") : L"") + AppendAttribute(L"y", command.parameters.count(L"y") ? command.parameters.at(L"y") : L"") + AppendAttribute(L"parallel", command.parameters.count(L"parallel") ? command.parameters.at(L"parallel") : L"") + L"]\r\n";
            break;
        case ScriptCommand::Type::Flash:
            output += L"[flash" + AppendAttribute(L"time", command.parameters.count(L"time") ? command.parameters.at(L"time") : L"") + AppendAttribute(L"color", command.parameters.count(L"color") ? command.parameters.at(L"color") : L"") + AppendAttribute(L"opacity", command.parameters.count(L"opacity") ? command.parameters.at(L"opacity") : L"") + AppendAttribute(L"parallel", command.parameters.count(L"parallel") ? command.parameters.at(L"parallel") : L"") + L"]\r\n";
            break;
        case ScriptCommand::Type::Tint:
            output += L"[tint" + AppendAttribute(L"color", command.parameters.count(L"color") ? command.parameters.at(L"color") : L"") + AppendAttribute(L"opacity", command.parameters.count(L"opacity") ? command.parameters.at(L"opacity") : L"") + L"]\r\n";
            break;
        case ScriptCommand::Type::SetValue:
            output += L"[set" + AppendAttribute(L"name", command.parameters.count(L"name") ? command.parameters.at(L"name") : L"") + AppendAttribute(L"value", command.parameters.count(L"value") ? command.parameters.at(L"value") : L"") + L"]\r\n";
            break;
        case ScriptCommand::Type::AddValue:
            output += L"[add" + AppendAttribute(L"name", command.parameters.count(L"name") ? command.parameters.at(L"name") : L"") + AppendAttribute(L"value", command.parameters.count(L"value") ? command.parameters.at(L"value") : L"") + L"]\r\n";
            break;
        case ScriptCommand::Type::IfJump:
            output += L"[if";
            output += AppendAttribute(L"name", command.parameters.count(L"name") ? command.parameters.at(L"name") : L"");
            output += AppendAttribute(L"op", command.parameters.count(L"op") ? command.parameters.at(L"op") : L"");
            output += AppendAttribute(L"value", command.parameters.count(L"value") ? command.parameters.at(L"value") : L"");
            output += AppendAttribute(L"target", command.parameters.count(L"target") ? command.parameters.at(L"target") : L"");
            output += L"]\r\n";
            break;
        case ScriptCommand::Type::Jump:
            output += L"[jump" + AppendAttribute(L"target", command.parameters.count(L"target") ? command.parameters.at(L"target") : L"") + L"]\r\n";
            break;
        case ScriptCommand::Type::Label:
            output += L"*" + (command.parameters.count(L"name") ? EscapeAttribute(command.parameters.at(L"name")) : L"") + L"\r\n";
            break;
        }
    }
    return output;
}

bool ParseScenario(const std::wstring& scenarioText, ScenarioDocument& document, std::wstring& errorMessage)
{
    document = ScenarioDocument{};

    std::wistringstream input(scenarioText);
    std::wstring line;
    size_t lineNumber = 0;

    while (std::getline(input, line))
    {
        ++lineNumber;
        if (!line.empty() && line.back() == L'\r')
        {
            line.pop_back();
        }

        const std::wstring trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == L';')
        {
            continue;
        }

        if (trimmed[0] == L'*')
        {
            const std::wstring labelName = Trim(trimmed.substr(1));
            if (labelName.empty())
            {
                errorMessage = L"空のラベルがあります。line=" + std::to_wstring(lineNumber);
                return false;
            }

            document.labels[labelName] = document.commands.size();
            ScriptCommand command = MakeCommand(ScriptCommand::Type::Label, lineNumber);
            SetCommandParameter(command, L"name", labelName);
            document.commands.push_back(std::move(command));
            continue;
        }

        if (!IsTagLine(trimmed))
        {
            ScriptCommand command = MakeCommand(ScriptCommand::Type::Text, lineNumber);
            SetCommandParameter(command, L"value", trimmed);
            document.commands.push_back(std::move(command));
            continue;
        }

        const std::wstring body = trimmed.substr(1, trimmed.size() - 2);
        const std::wstring tagName = GetTagName(body);

        if (tagName == L"title")
        {
            ScriptCommand command = MakeCommand(ScriptCommand::Type::Title, lineNumber);
            const std::wstring title = GetAttributeValue(body, L"name");
            SetCommandParameter(command, L"name", title);
            document.title = title.empty() ? document.title : title;
            document.commands.push_back(std::move(command));
            continue;
        }

        if (tagName == L"bg")
        {
            ScriptCommand command = MakeCommand(ScriptCommand::Type::Background, lineNumber);
            SetCommandParameter(command, L"name", GetAttributeValue(body, L"name"));
            SetCommandParameter(command, L"storage", GetAttributeValue(body, L"storage"));
            SetCommandParameter(command, L"color", GetAttributeValue(body, L"color"));
            SetCommandParameter(command, L"visible", GetAttributeValue(body, L"visible"));
            SetCommandParameter(command, L"x", GetAttributeValue(body, L"x"));
            SetCommandParameter(command, L"y", GetAttributeValue(body, L"y"));
            SetCommandParameter(command, L"scale", GetAttributeValue(body, L"scale"));
            SetCommandParameter(command, L"opacity", GetAttributeValue(body, L"opacity"));
            SetCommandParameter(command, L"fade", GetAttributeValue(body, L"fade"));
            SetCommandParameter(command, L"fade_time", GetAttributeValue(body, L"fade_time"));
            SetCommandParameter(command, L"fade_wait", GetAttributeValue(body, L"fade_wait"));
            document.commands.push_back(std::move(command));
            continue;
        }

        if (tagName == L"ch")
        {
            ScriptCommand command = MakeCommand(ScriptCommand::Type::Character, lineNumber);
            SetCommandParameter(command, L"pos", GetAttributeValue(body, L"pos"));
            SetCommandParameter(command, L"storage", GetAttributeValue(body, L"storage"));
            SetCommandParameter(command, L"name", GetAttributeValue(body, L"name"));
            SetCommandParameter(command, L"visible", GetAttributeValue(body, L"visible"));
            SetCommandParameter(command, L"x", GetAttributeValue(body, L"x"));
            SetCommandParameter(command, L"y", GetAttributeValue(body, L"y"));
            SetCommandParameter(command, L"scale", GetAttributeValue(body, L"scale"));
            SetCommandParameter(command, L"opacity", GetAttributeValue(body, L"opacity"));
            document.commands.push_back(std::move(command));
            continue;
        }

        if (tagName == L"hidech")
        {
            ScriptCommand command = MakeCommand(ScriptCommand::Type::HideCharacter, lineNumber);
            SetCommandParameter(command, L"pos", GetAttributeValue(body, L"pos"));
            SetCommandParameter(command, L"fade", GetAttributeValue(body, L"fade"));
            SetCommandParameter(command, L"fade_time", GetAttributeValue(body, L"fade_time"));
            SetCommandParameter(command, L"fade_wait", GetAttributeValue(body, L"fade_wait"));
            document.commands.push_back(std::move(command));
            continue;
        }

        if (tagName == L"speaker")
        {
            ScriptCommand command = MakeCommand(ScriptCommand::Type::Speaker, lineNumber);
            SetCommandParameter(command, L"name", GetAttributeValue(body, L"name"));
            document.commands.push_back(std::move(command));
            continue;
        }

        if (tagName == L"clear_speaker")
        {
            document.commands.push_back(MakeCommand(ScriptCommand::Type::ClearSpeaker, lineNumber));
            continue;
        }

        if (tagName == L"text")
        {
            ScriptCommand command = MakeCommand(ScriptCommand::Type::Text, lineNumber);
            SetCommandParameter(command, L"value", GetAttributeValue(body, L"value"));
            SetCommandParameter(command, L"name_visible", GetAttributeValue(body, L"name_visible"));
            SetCommandParameter(command, L"name_target", GetAttributeValue(body, L"name_target"));
            SetCommandParameter(command, L"name_x", GetAttributeValue(body, L"name_x"));
            SetCommandParameter(command, L"name_y", GetAttributeValue(body, L"name_y"));
            SetCommandParameter(command, L"name_image", GetAttributeValue(body, L"name_image"));
            document.commands.push_back(std::move(command));
            continue;
        }

        if (tagName == L"choice")
        {
            ScriptCommand command = MakeCommand(ScriptCommand::Type::Choice, lineNumber);
            SetCommandParameter(command, L"prompt", GetAttributeValue(body, L"prompt"));
            SetCommandParameter(command, L"x", GetAttributeValue(body, L"x"));
            SetCommandParameter(command, L"y", GetAttributeValue(body, L"y"));

            while (std::getline(input, line))
            {
                ++lineNumber;
                if (!line.empty() && line.back() == L'\r')
                {
                    line.pop_back();
                }

                const std::wstring nested = Trim(line);
                if (nested.empty() || nested[0] == L';')
                {
                    continue;
                }

                if (!IsTagLine(nested))
                {
                    errorMessage = L"choice ブロックの閉じタグが見つかりません。line=" + std::to_wstring(lineNumber);
                    return false;
                }

                const std::wstring nestedBody = nested.substr(1, nested.size() - 2);
                const std::wstring nestedTag = GetTagName(nestedBody);
                if (nestedTag == L"option")
                {
                    const std::wstring text = GetAttributeValue(nestedBody, L"text");
                    const std::wstring target = GetAttributeValue(nestedBody, L"target");
                    if (text.empty() || target.empty())
                    {
                        errorMessage = L"option には text と target が必要です。line=" + std::to_wstring(lineNumber);
                        return false;
                    }

                    command.links.push_back({ text, target });
                    continue;
                }

                if (nestedTag == L"endchoice")
                {
                    break;
                }

                errorMessage = L"choice ブロック内に未対応タグがあります。line=" + std::to_wstring(lineNumber) + L" tag=[" + nestedBody + L"]";
                return false;
            }

            if (command.links.empty())
            {
                errorMessage = L"choice に option がありません。line=" + std::to_wstring(command.sourceLine);
                return false;
            }

            document.commands.push_back(std::move(command));
            continue;
        }

        if (tagName == L"bgm")
        {
            ScriptCommand command = MakeCommand(ScriptCommand::Type::Bgm, lineNumber);
            SetCommandParameter(command, L"category", GetAttributeValue(body, L"category"));
            SetCommandParameter(command, L"storage", GetAttributeValue(body, L"storage"));
            SetCommandParameter(command, L"volume", GetAttributeValue(body, L"volume"));
            SetCommandParameter(command, L"loop", GetAttributeValue(body, L"loop"));
            SetCommandParameter(command, L"fadein", GetAttributeValue(body, L"fadein"));
            SetCommandParameter(command, L"fadeout", GetAttributeValue(body, L"fadeout"));
            document.commands.push_back(std::move(command));
            continue;
        }

        if (tagName == L"stopbgm")
        {
            document.commands.push_back(MakeCommand(ScriptCommand::Type::StopBgm, lineNumber));
            continue;
        }

        if (tagName == L"se")
        {
            ScriptCommand command = MakeCommand(ScriptCommand::Type::Se, lineNumber);
            SetCommandParameter(command, L"category", GetAttributeValue(body, L"category"));
            SetCommandParameter(command, L"storage", GetAttributeValue(body, L"storage"));
            SetCommandParameter(command, L"volume", GetAttributeValue(body, L"volume"));
            SetCommandParameter(command, L"loop", GetAttributeValue(body, L"loop"));
            SetCommandParameter(command, L"fadein", GetAttributeValue(body, L"fadein"));
            SetCommandParameter(command, L"fadeout", GetAttributeValue(body, L"fadeout"));
            document.commands.push_back(std::move(command));
            continue;
        }

        if (tagName == L"voice")
        {
            ScriptCommand command = MakeCommand(ScriptCommand::Type::Voice, lineNumber);
            SetCommandParameter(command, L"category", GetAttributeValue(body, L"category"));
            SetCommandParameter(command, L"storage", GetAttributeValue(body, L"storage"));
            SetCommandParameter(command, L"volume", GetAttributeValue(body, L"volume"));
            SetCommandParameter(command, L"loop", GetAttributeValue(body, L"loop"));
            SetCommandParameter(command, L"fadein", GetAttributeValue(body, L"fadein"));
            SetCommandParameter(command, L"fadeout", GetAttributeValue(body, L"fadeout"));
            document.commands.push_back(std::move(command));
            continue;
        }

        if (tagName == L"wait")
        {
            ScriptCommand command = MakeCommand(ScriptCommand::Type::Wait, lineNumber);
            SetCommandParameter(command, L"time", GetAttributeValue(body, L"time"));
            document.commands.push_back(std::move(command));
            continue;
        }

        if (tagName == L"cleartext")
        {
            document.commands.push_back(MakeCommand(ScriptCommand::Type::ClearText, lineNumber));
            continue;
        }

        if (tagName == L"messagewindow")
        {
            ScriptCommand command = MakeCommand(ScriptCommand::Type::MessageWindow, lineNumber);
            SetCommandParameter(command, L"visible", GetAttributeValue(body, L"visible"));
            document.commands.push_back(std::move(command));
            continue;
        }

        if (tagName == L"textspeed")
        {
            ScriptCommand command = MakeCommand(ScriptCommand::Type::TextSpeed, lineNumber);
            SetCommandParameter(command, L"value", GetAttributeValue(body, L"value"));
            document.commands.push_back(std::move(command));
            continue;
        }

        if (tagName == L"font")
        {
            ScriptCommand command = MakeCommand(ScriptCommand::Type::MessageFont, lineNumber);
            SetCommandParameter(command, L"face", GetAttributeValue(body, L"face"));
            document.commands.push_back(std::move(command));
            continue;
        }

        if (tagName == L"fontreset")
        {
            document.commands.push_back(MakeCommand(ScriptCommand::Type::MessageFontReset, lineNumber));
            continue;
        }

        if (tagName == L"messageui")
        {
            ScriptCommand command = MakeCommand(ScriptCommand::Type::MessageStyle, lineNumber);
            SetCommandParameter(command, L"color", GetAttributeValue(body, L"color"));
            SetCommandParameter(command, L"border", GetAttributeValue(body, L"border"));
            SetCommandParameter(command, L"opacity", GetAttributeValue(body, L"opacity"));
            SetCommandParameter(command, L"padding", GetAttributeValue(body, L"padding"));
            SetCommandParameter(command, L"image", GetAttributeValue(body, L"image"));
            document.commands.push_back(std::move(command));
            continue;
        }

        if (tagName == L"textcolor")
        {
            ScriptCommand command = MakeCommand(ScriptCommand::Type::TextColor, lineNumber);
            SetCommandParameter(command, L"color", GetAttributeValue(body, L"color"));
            document.commands.push_back(std::move(command));
            continue;
        }

        if (tagName == L"namecolor")
        {
            ScriptCommand command = MakeCommand(ScriptCommand::Type::NameColor, lineNumber);
            SetCommandParameter(command, L"color", GetAttributeValue(body, L"color"));
            document.commands.push_back(std::move(command));
            continue;
        }

        if (tagName == L"namewindow")
        {
            ScriptCommand command = MakeCommand(ScriptCommand::Type::NameWindow, lineNumber);
            SetCommandParameter(command, L"visible", GetAttributeValue(body, L"visible"));
            document.commands.push_back(std::move(command));
            continue;
        }

        if (tagName == L"vertical")
        {
            ScriptCommand command = MakeCommand(ScriptCommand::Type::VerticalText, lineNumber);
            SetCommandParameter(command, L"enabled", GetAttributeValue(body, L"enabled"));
            document.commands.push_back(std::move(command));
            continue;
        }

        if (tagName == L"page")
        {
            document.commands.push_back(MakeCommand(ScriptCommand::Type::PageBreak, lineNumber));
            continue;
        }

        if (tagName == L"shake")
        {
            ScriptCommand command = MakeCommand(ScriptCommand::Type::Shake, lineNumber);
            SetCommandParameter(command, L"time", GetAttributeValue(body, L"time"));
            SetCommandParameter(command, L"power", GetAttributeValue(body, L"power"));
            document.commands.push_back(std::move(command));
            continue;
        }

        if (tagName == L"fade")
        {
            ScriptCommand command = MakeCommand(ScriptCommand::Type::Fade, lineNumber);
            SetCommandParameter(command, L"time", GetAttributeValue(body, L"time"));
            SetCommandParameter(command, L"color", GetAttributeValue(body, L"color"));
            SetCommandParameter(command, L"opacity", GetAttributeValue(body, L"opacity"));
            SetCommandParameter(command, L"parallel", GetAttributeValue(body, L"parallel"));
            SetCommandParameter(command, L"target", GetAttributeValue(body, L"target"));
            document.commands.push_back(std::move(command));
            continue;
        }

        if (tagName == L"transition")
        {
            ScriptCommand command = MakeCommand(ScriptCommand::Type::Transition, lineNumber);
            SetCommandParameter(command, L"time", GetAttributeValue(body, L"time"));
            SetCommandParameter(command, L"style", GetAttributeValue(body, L"style"));
            SetCommandParameter(command, L"color", GetAttributeValue(body, L"color"));
            SetCommandParameter(command, L"parallel", GetAttributeValue(body, L"parallel"));
            SetCommandParameter(command, L"target", GetAttributeValue(body, L"target"));
            document.commands.push_back(std::move(command));
            continue;
        }

        if (tagName == L"zoom")
        {
            ScriptCommand command = MakeCommand(ScriptCommand::Type::Zoom, lineNumber);
            SetCommandParameter(command, L"time", GetAttributeValue(body, L"time"));
            SetCommandParameter(command, L"scale", GetAttributeValue(body, L"scale"));
            SetCommandParameter(command, L"parallel", GetAttributeValue(body, L"parallel"));
            document.commands.push_back(std::move(command));
            continue;
        }

        if (tagName == L"pan")
        {
            ScriptCommand command = MakeCommand(ScriptCommand::Type::Pan, lineNumber);
            SetCommandParameter(command, L"time", GetAttributeValue(body, L"time"));
            SetCommandParameter(command, L"x", GetAttributeValue(body, L"x"));
            SetCommandParameter(command, L"y", GetAttributeValue(body, L"y"));
            SetCommandParameter(command, L"parallel", GetAttributeValue(body, L"parallel"));
            document.commands.push_back(std::move(command));
            continue;
        }

        if (tagName == L"flash")
        {
            ScriptCommand command = MakeCommand(ScriptCommand::Type::Flash, lineNumber);
            SetCommandParameter(command, L"time", GetAttributeValue(body, L"time"));
            SetCommandParameter(command, L"color", GetAttributeValue(body, L"color"));
            SetCommandParameter(command, L"opacity", GetAttributeValue(body, L"opacity"));
            SetCommandParameter(command, L"parallel", GetAttributeValue(body, L"parallel"));
            document.commands.push_back(std::move(command));
            continue;
        }

        if (tagName == L"tint")
        {
            ScriptCommand command = MakeCommand(ScriptCommand::Type::Tint, lineNumber);
            SetCommandParameter(command, L"color", GetAttributeValue(body, L"color"));
            SetCommandParameter(command, L"opacity", GetAttributeValue(body, L"opacity"));
            document.commands.push_back(std::move(command));
            continue;
        }

        if (tagName == L"set")
        {
            ScriptCommand command = MakeCommand(ScriptCommand::Type::SetValue, lineNumber);
            SetCommandParameter(command, L"name", GetAttributeValue(body, L"name"));
            SetCommandParameter(command, L"value", GetAttributeValue(body, L"value"));
            document.commands.push_back(std::move(command));
            continue;
        }

        if (tagName == L"add")
        {
            ScriptCommand command = MakeCommand(ScriptCommand::Type::AddValue, lineNumber);
            SetCommandParameter(command, L"name", GetAttributeValue(body, L"name"));
            SetCommandParameter(command, L"value", GetAttributeValue(body, L"value"));
            document.commands.push_back(std::move(command));
            continue;
        }

        if (tagName == L"if")
        {
            ScriptCommand command = MakeCommand(ScriptCommand::Type::IfJump, lineNumber);
            SetCommandParameter(command, L"name", GetAttributeValue(body, L"name"));
            SetCommandParameter(command, L"value", GetAttributeValue(body, L"value"));
            SetCommandParameter(command, L"op", GetAttributeValue(body, L"op"));
            SetCommandParameter(command, L"target", GetAttributeValue(body, L"target"));
            document.commands.push_back(std::move(command));
            continue;
        }

        if (tagName == L"jump")
        {
            ScriptCommand command = MakeCommand(ScriptCommand::Type::Jump, lineNumber);
            SetCommandParameter(command, L"target", GetAttributeValue(body, L"target"));
            document.commands.push_back(std::move(command));
            continue;
        }

        errorMessage = L"未対応のタグがあります。line=" + std::to_wstring(lineNumber) + L" tag=[" + body + L"]";
        return false;
    }

    return true;
}
