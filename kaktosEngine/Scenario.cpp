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
            output += AppendAttribute(L"storage", command.parameters.count(L"storage") ? command.parameters.at(L"storage") : L"");
            output += AppendAttribute(L"color", command.parameters.count(L"color") ? command.parameters.at(L"color") : L"");
            output += L"]\r\n";
            break;
        case ScriptCommand::Type::Character:
            output += L"[ch";
            output += AppendAttribute(L"pos", command.parameters.count(L"pos") ? command.parameters.at(L"pos") : L"");
            output += AppendAttribute(L"name", command.parameters.count(L"name") ? command.parameters.at(L"name") : L"");
            output += AppendAttribute(L"storage", command.parameters.count(L"storage") ? command.parameters.at(L"storage") : L"");
            output += L"]\r\n";
            break;
        case ScriptCommand::Type::HideCharacter:
            output += L"[hidech" + AppendAttribute(L"pos", command.parameters.count(L"pos") ? command.parameters.at(L"pos") : L"") + L"]\r\n";
            break;
        case ScriptCommand::Type::Speaker:
            output += L"[speaker" + AppendAttribute(L"name", command.parameters.count(L"name") ? command.parameters.at(L"name") : L"") + L"]\r\n";
            break;
        case ScriptCommand::Type::ClearSpeaker:
            output += L"[clear_speaker]\r\n";
            break;
        case ScriptCommand::Type::Text:
            output += L"[text" + AppendAttribute(L"value", command.parameters.count(L"value") ? command.parameters.at(L"value") : L"") + L"]\r\n";
            break;
        case ScriptCommand::Type::Choice:
            output += L"[choice" + AppendAttribute(L"prompt", command.parameters.count(L"prompt") ? command.parameters.at(L"prompt") : L"") + L"]\r\n";
            for (const auto& link : command.links)
            {
                output += L"[option" + AppendAttribute(L"text", link.first) + AppendAttribute(L"target", link.second) + L"]\r\n";
            }
            output += L"[endchoice]\r\n";
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
            SetCommandParameter(command, L"storage", GetAttributeValue(body, L"storage"));
            SetCommandParameter(command, L"color", GetAttributeValue(body, L"color"));
            document.commands.push_back(std::move(command));
            continue;
        }

        if (tagName == L"ch")
        {
            ScriptCommand command = MakeCommand(ScriptCommand::Type::Character, lineNumber);
            SetCommandParameter(command, L"pos", GetAttributeValue(body, L"pos"));
            SetCommandParameter(command, L"storage", GetAttributeValue(body, L"storage"));
            SetCommandParameter(command, L"name", GetAttributeValue(body, L"name"));
            document.commands.push_back(std::move(command));
            continue;
        }

        if (tagName == L"hidech")
        {
            ScriptCommand command = MakeCommand(ScriptCommand::Type::HideCharacter, lineNumber);
            SetCommandParameter(command, L"pos", GetAttributeValue(body, L"pos"));
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
            document.commands.push_back(std::move(command));
            continue;
        }

        if (tagName == L"choice")
        {
            ScriptCommand command = MakeCommand(ScriptCommand::Type::Choice, lineNumber);
            SetCommandParameter(command, L"prompt", GetAttributeValue(body, L"prompt"));

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
                    errorMessage = L"choice ブロック内はタグのみ対応。line=" + std::to_wstring(lineNumber);
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
                        errorMessage = L"option には text と target が必要。line=" + std::to_wstring(lineNumber);
                        return false;
                    }

                    command.links.push_back({ text, target });
                    continue;
                }

                if (nestedTag == L"endchoice")
                {
                    break;
                }

                errorMessage = L"choice ブロック内の未対応タグ。line=" + std::to_wstring(lineNumber) + L" tag=[" + nestedBody + L"]";
                return false;
            }

            if (command.links.empty())
            {
                errorMessage = L"choice に option がない。line=" + std::to_wstring(command.sourceLine);
                return false;
            }

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
