#pragma once

#include "framework.h"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct ScriptCommand
{
    enum class Type
    {
        Title,
        Background,
        Character,
        HideCharacter,
        Speaker,
        ClearSpeaker,
        Text,
        Choice,
        Bgm,
        StopBgm,
        Se,
        Voice,
        Wait,
        ClearText,
        MessageWindow,
        TextSpeed,
        MessageFont,
        MessageFontReset,
        MessageStyle,
        TextColor,
        NameColor,
        NameWindow,
        VerticalText,
        PageBreak,
        Shake,
        Fade,
        Transition,
        Zoom,
        Pan,
        Flash,
        Tint,
        SetValue,
        AddValue,
        IfJump,
        Jump,
        Label,
    };

    Type type = Type::Text;
    std::wstring id;
    std::unordered_map<std::wstring, std::wstring> parameters;
    std::vector<std::pair<std::wstring, std::wstring>> links;
    size_t sourceLine = 0;
};

struct ScenarioDocument
{
    std::wstring title = L"Kaktos Engine";
    std::vector<ScriptCommand> commands;
    std::unordered_map<std::wstring, size_t> labels;
};

std::wstring Trim(const std::wstring& text);
std::wstring Utf8ToWide(const std::string& text);
std::wstring GetDirectoryPath(const std::wstring& path);
std::wstring CombinePath(const std::wstring& baseDir, const std::wstring& relativePath);
std::vector<std::wstring> GetScenarioCandidates();
std::vector<std::wstring> EnumerateFiles(const std::wstring& directory, const std::wstring& pattern);
std::wstring GetFileNamePart(const std::wstring& path);
std::wstring GetFileStemPart(const std::wstring& path);
std::wstring MakeRelativePath(const std::wstring& fullPath, const std::wstring& baseDir);
bool TryReadTextFile(const std::wstring& path, std::wstring& content);
bool TryWriteTextFile(const std::wstring& path, const std::wstring& content);
bool ParseScenario(const std::wstring& scenarioText, ScenarioDocument& document, std::wstring& errorMessage);
std::wstring SerializeScenario(const ScenarioDocument& document);
void RebuildScenarioLabels(ScenarioDocument& document);
