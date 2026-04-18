#include "framework.h"
#include "NovelRuntime.h"
#include "resource.h"

#include <commdlg.h>
#include <sstream>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "msimg32.lib")

bool NovelRuntime::Initialize()
{
    if (gdiplusToken_ != 0)
    {
        return true;
    }

    Gdiplus::GdiplusStartupInput startupInput;
    return Gdiplus::GdiplusStartup(&gdiplusToken_, &startupInput, nullptr) == Gdiplus::Ok;
}

void NovelRuntime::Shutdown()
{
    if (gdiplusToken_ != 0)
    {
        Gdiplus::GdiplusShutdown(gdiplusToken_);
        gdiplusToken_ = 0;
    }
}

const std::wstring& NovelRuntime::GetWindowTitle() const
{
    return storyTitle_;
}

RECT NovelRuntime::GetToolbarRect(const RECT& previewRect) const
{
    return RECT{ previewRect.left + 16, previewRect.top + 12, previewRect.right - 16, previewRect.top + 62 };
}

RECT NovelRuntime::GetLeftPanelRect(const RECT& clientRect) const
{
    if (!showComponents_)
    {
        return RECT{ clientRect.left, clientRect.top, clientRect.left, clientRect.bottom };
    }
    const int clientWidth = static_cast<int>(clientRect.right - clientRect.left);
    const int maxWidth = clientWidth - rightPanelWidth_ - 340;
    const int width = (std::max)(220, (std::min)(leftPanelWidth_, maxWidth));
    return RECT{ clientRect.left, clientRect.top, clientRect.left + width, clientRect.bottom };
}

RECT NovelRuntime::GetRightPanelRect(const RECT& clientRect) const
{
    if (!showInspector_)
    {
        return RECT{ clientRect.right, clientRect.top, clientRect.right, clientRect.bottom };
    }
    const int clientWidth = static_cast<int>(clientRect.right - clientRect.left);
    const int maxWidth = clientWidth - leftPanelWidth_ - 340;
    const int width = (std::max)(260, (std::min)(rightPanelWidth_, maxWidth));
    return RECT{ clientRect.right - width, clientRect.top, clientRect.right, clientRect.bottom };
}

RECT NovelRuntime::GetPreviewRect(const RECT& clientRect) const
{
    const RECT leftRect = GetLeftPanelRect(clientRect);
    const RECT rightRect = GetRightPanelRect(clientRect);
    const LONG left = showComponents_ ? leftRect.right + 8 : clientRect.left + 8;
    const LONG right = showInspector_ ? rightRect.left - 8 : clientRect.right - 8;
    return RECT{ left, clientRect.top, right, clientRect.bottom };
}

int NovelRuntime::GetPreviewContentTop(const RECT& previewRect) const
{
    int currentTop = GetToolbarRect(previewRect).bottom + 12;
    if (showFlowGraph_)
    {
        currentTop += graphHeight_ + 18;
    }
    if (showEventList_)
    {
        currentTop += eventListHeight_ + 18;
    }
    return currentTop;
}

bool NovelRuntime::HasVisibleArea(const RECT& rect) const
{
    return rect.right > rect.left && rect.bottom > rect.top;
}

RECT NovelRuntime::GetGraphRect(const RECT& previewRect) const
{
    if (!showFlowGraph_)
    {
        const int top = GetToolbarRect(previewRect).bottom + 12;
        return RECT{ previewRect.left + 16, top, previewRect.right - 16, top };
    }
    const RECT toolbarRect = GetToolbarRect(previewRect);
    const int top = toolbarRect.bottom + 12;
    const int bottom = top + (std::max)(120, graphHeight_);
    return RECT{ previewRect.left + 16, top, previewRect.right - 16, bottom };
}

RECT NovelRuntime::GetStageRect(const RECT& previewRect) const
{
    if (!showPreviewPanel_)
    {
        const int top = GetPreviewContentTop(previewRect);
        return RECT{ previewRect.left + 16, top, previewRect.right - 16, top };
    }
    const int top = GetPreviewContentTop(previewRect);
    return RECT{ previewRect.left + 16, top, previewRect.right - 16, previewRect.bottom - 210 };
}

RECT NovelRuntime::GetMessageRect(const RECT& previewRect) const
{
    if (!showPreviewPanel_)
    {
        return RECT{ previewRect.left + 16, previewRect.bottom - 20, previewRect.right - 16, previewRect.bottom - 20 };
    }
    return RECT{ previewRect.left + 16, previewRect.bottom - 190, previewRect.right - 16, previewRect.bottom - 20 };
}

RECT NovelRuntime::GetEventListRect(const RECT& previewRect) const
{
    if (!showEventList_)
    {
        const RECT graphRect = GetGraphRect(previewRect);
        const int top = graphRect.bottom + (showFlowGraph_ ? 18 : 0);
        return RECT{ previewRect.left + 16, top, previewRect.right - 16, top };
    }
    const RECT graphRect = GetGraphRect(previewRect);
    const int top = graphRect.bottom + (showFlowGraph_ ? 18 : 0);
    const int bottom = top + (std::max)(140, eventListHeight_);
    return RECT{ previewRect.left + 16, top, previewRect.right - 16, bottom };
}

void NovelRuntime::InitializeToolbarItems()
{
    toolbarItems_.clear();
    toolbarItems_.push_back(ToolbarItem{ L"preview", L"\u30d7\u30ec\u30d3\u30e5\u30fc", L"ui\\preview.png", nullptr });
    toolbarItems_.push_back(ToolbarItem{ L"save", L"\u4fdd\u5b58", L"ui\\save.png", nullptr });
    toolbarItems_.push_back(ToolbarItem{ L"project", L"\u30d7\u30ed\u30b8\u30a7\u30af\u30c8", L"ui\\project.png", nullptr });
    toolbarItems_.push_back(ToolbarItem{ L"config", L"\u8a2d\u5b9a", L"ui\\config.png", nullptr });
    toolbarItems_.push_back(ToolbarItem{ L"layout", L"\u30ec\u30a4\u30a2\u30a6\u30c8", L"ui\\layout.png", nullptr });
    toolbarItems_.push_back(ToolbarItem{ L"build", L"\u66f8\u304d\u51fa\u3057", L"ui\\build.png", nullptr });
}

void NovelRuntime::LoadToolbarIcons()
{
    for (ToolbarItem& item : toolbarItems_)
    {
        item.iconImage.reset();

        std::vector<std::wstring> candidates;
        if (!item.iconPath.empty())
        {
            candidates.push_back(CombinePath(scenarioBaseDir_, item.iconPath));
            candidates.push_back(item.iconPath);

            const size_t dot = item.iconPath.find_last_of(L'.');
            if (dot != std::wstring::npos)
            {
                const std::wstring stem = item.iconPath.substr(0, dot);
                candidates.push_back(CombinePath(scenarioBaseDir_, stem + L".jpg"));
                candidates.push_back(stem + L".jpg");
                candidates.push_back(CombinePath(scenarioBaseDir_, stem + L".jpeg"));
                candidates.push_back(stem + L".jpeg");
            }
        }

        for (const std::wstring& candidate : candidates)
        {
            auto image = TryLoadImage(candidate);
            if (image)
            {
                item.iconImage = std::move(image);
                break;
            }
        }
    }
}

std::wstring NovelRuntime::GetCommandParameter(const ScriptCommand& command, const std::wstring& key) const
{
    const auto found = command.parameters.find(key);
    return found == command.parameters.end() ? L"" : found->second;
}

bool NovelRuntime::TryParseHexColor(const std::wstring& value, COLORREF& color) const
{
    const std::wstring text = Trim(value);
    if (text.size() == 7 && text[0] == L'#')
    {
        const int red = std::stoi(text.substr(1, 2), nullptr, 16);
        const int green = std::stoi(text.substr(3, 2), nullptr, 16);
        const int blue = std::stoi(text.substr(5, 2), nullptr, 16);
        color = RGB(red, green, blue);
        return true;
    }

    return false;
}

bool NovelRuntime::TryGetNumber(const std::wstring& value, long long& number) const
{
    if (value.empty())
    {
        return false;
    }

    wchar_t* endPtr = nullptr;
    number = wcstoll(value.c_str(), &endPtr, 10);
    return endPtr != value.c_str() && endPtr != nullptr && *endPtr == L'\0';
}

std::unique_ptr<Gdiplus::Image> NovelRuntime::TryLoadImage(const std::wstring& fullPath) const
{
    if (fullPath.empty())
    {
        return nullptr;
    }

    auto image = std::make_unique<Gdiplus::Image>(fullPath.c_str());
    if (image->GetLastStatus() != Gdiplus::Ok)
    {
        return nullptr;
    }

    return image;
}

CharacterSlot* NovelRuntime::GetCharacterSlot(const std::wstring& position)
{
    const std::wstring normalized = Trim(position);
    if (normalized == L"left")
    {
        return &leftCharacter_;
    }
    if (normalized == L"center")
    {
        return &centerCharacter_;
    }
    if (normalized == L"right")
    {
        return &rightCharacter_;
    }

    return nullptr;
}

void NovelRuntime::ApplyBackgroundCommand(const ScriptCommand& command)
{
    const std::wstring colorValue = GetCommandParameter(command, L"color");
    if (!colorValue.empty())
    {
        COLORREF color = backgroundColor_;
        try
        {
            if (TryParseHexColor(colorValue, color))
            {
                backgroundColor_ = color;
                backgroundImage_.reset();
                backgroundPath_ = L"solid";
                statusText_ = L"background color changed";
                return;
            }
        }
        catch (...)
        {
        }

        statusText_ = L"invalid background color";
        return;
    }

    const std::wstring relativePath = GetCommandParameter(command, L"storage");
    if (!relativePath.empty())
    {
        std::wstring fullPath = CombinePath(scenarioBaseDir_, relativePath);
        auto image = TryLoadImage(fullPath);
        if (!image)
        {
            image = TryLoadImage(relativePath);
            fullPath = relativePath;
        }

        if (image)
        {
            backgroundImage_ = std::move(image);
            backgroundPath_ = fullPath;
            statusText_ = L"background image: " + relativePath;
        }
        else
        {
            statusText_ = L"background image missing: " + relativePath;
        }
    }
}

void NovelRuntime::ApplyCharacterCommand(const ScriptCommand& command)
{
    const std::wstring position = GetCommandParameter(command, L"pos");
    const std::wstring storage = GetCommandParameter(command, L"storage");
    const std::wstring name = GetCommandParameter(command, L"name");
    if (position.empty())
    {
        statusText_ = L"character command is missing pos";
        return;
    }

    CharacterSlot* slot = GetCharacterSlot(position);
    if (!slot)
    {
        statusText_ = L"unknown character slot: " + position;
        return;
    }

    slot->displayName = name;
    slot->imagePath.clear();
    slot->image.reset();

    if (!storage.empty())
    {
        std::wstring fullPath = CombinePath(scenarioBaseDir_, storage);
        auto image = TryLoadImage(fullPath);
        if (!image)
        {
            image = TryLoadImage(storage);
            fullPath = storage;
        }

        if (image)
        {
            slot->imagePath = fullPath;
            slot->image = std::move(image);
            statusText_ = L"character shown: " + slot->slotName;
            return;
        }

        statusText_ = L"character image missing: " + storage;
        return;
    }

    statusText_ = L"character placeholder: " + slot->slotName;
}

void NovelRuntime::ApplyHideCharacterCommand(const ScriptCommand& command)
{
    const std::wstring normalized = Trim(GetCommandParameter(command, L"pos"));
    if (normalized.empty() || normalized == L"all")
    {
        leftCharacter_ = { L"left" };
        centerCharacter_ = { L"center" };
        rightCharacter_ = { L"right" };
        statusText_ = L"all characters hidden";
        return;
    }

    CharacterSlot* slot = GetCharacterSlot(normalized);
    if (!slot)
    {
        statusText_ = L"unknown character slot: " + normalized;
        return;
    }

    const std::wstring slotName = slot->slotName;
    *slot = { slotName };
    statusText_ = L"character hidden: " + slot->slotName;
}

void NovelRuntime::ApplySetValueCommand(const ScriptCommand& command)
{
    const std::wstring name = GetCommandParameter(command, L"name");
    if (name.empty())
    {
        statusText_ = L"set requires name";
        return;
    }

    variables_[name] = GetCommandParameter(command, L"value");
    statusText_ = L"set variable: " + name;
}

void NovelRuntime::ApplyAddValueCommand(const ScriptCommand& command)
{
    const std::wstring name = GetCommandParameter(command, L"name");
    if (name.empty())
    {
        statusText_ = L"add requires name";
        return;
    }

    long long current = 0;
    long long delta = 0;
    TryGetNumber(variables_[name], current);
    if (!TryGetNumber(GetCommandParameter(command, L"value"), delta))
    {
        statusText_ = L"add requires numeric value";
        return;
    }

    variables_[name] = std::to_wstring(current + delta);
    statusText_ = L"add variable: " + name;
}

bool NovelRuntime::EvaluateCondition(const ScriptCommand& command) const
{
    const std::wstring name = GetCommandParameter(command, L"name");
    const std::wstring op = GetCommandParameter(command, L"op");
    const std::wstring right = GetCommandParameter(command, L"value");
    const auto found = variables_.find(name);
    const std::wstring left = found == variables_.end() ? L"" : found->second;
    const std::wstring normalizedOp = op.empty() ? L"eq" : op;

    long long leftNumber = 0;
    long long rightNumber = 0;
    const bool numeric = TryGetNumber(left, leftNumber) && TryGetNumber(right, rightNumber);

    if (normalizedOp == L"eq")
    {
        return numeric ? leftNumber == rightNumber : left == right;
    }
    if (normalizedOp == L"ne")
    {
        return numeric ? leftNumber != rightNumber : left != right;
    }
    if (normalizedOp == L"gt" && numeric)
    {
        return leftNumber > rightNumber;
    }
    if (normalizedOp == L"ge" && numeric)
    {
        return leftNumber >= rightNumber;
    }
    if (normalizedOp == L"lt" && numeric)
    {
        return leftNumber < rightNumber;
    }
    if (normalizedOp == L"le" && numeric)
    {
        return leftNumber <= rightNumber;
    }

    return false;
}

bool NovelRuntime::JumpToLabel(const std::wstring& target)
{
    const auto found = scenario_.labels.find(target);
    if (found == scenario_.labels.end())
    {
        currentText_ = L"jump target not found: " + target;
        reachedEnd_ = true;
        return false;
    }

    currentCommandIndex_ = found->second;
    return true;
}

void NovelRuntime::ApplyIfJumpCommand(const ScriptCommand& command)
{
    if (EvaluateCondition(command))
    {
        JumpToLabel(GetCommandParameter(command, L"target"));
        statusText_ = L"condition: true";
    }
    else
    {
        statusText_ = L"condition: false";
    }
}

void NovelRuntime::ActivateChoice(const ScriptCommand& command)
{
    activeChoices_ = command.links;
    activeChoiceRects_.assign(command.links.size(), RECT{});
    waitingForChoice_ = true;
    currentText_ = GetCommandParameter(command, L"prompt");
    if (currentText_.empty())
    {
        currentText_ = L"Choose an option.";
    }
    statusText_ = L"waiting for choice";
}

void NovelRuntime::SelectChoice(size_t index)
{
    if (index >= activeChoices_.size())
    {
        return;
    }

    const std::wstring target = activeChoices_[index].second;
    activeChoices_.clear();
    activeChoiceRects_.clear();
    waitingForChoice_ = false;
    statusText_ = L"choice: " + std::to_wstring(index + 1);
    if (JumpToLabel(target))
    {
        Advance();
    }
}

void NovelRuntime::LoadScenario(const std::wstring& requestedPath)
{
    backgroundPath_.clear();
    backgroundImage_.reset();
    backgroundColor_ = RGB(28, 36, 48);
    leftCharacter_ = { L"left" };
    centerCharacter_ = { L"center" };
    rightCharacter_ = { L"right" };
    storyTitle_ = L"Kaktos Engine";
    speakerName_.clear();
    currentText_ = L"Loading scenario...";
    statusText_.clear();
    scenarioBaseDir_.clear();
    scenarioPath_.clear();
    projectPath_.clear();
    scenario_ = ScenarioDocument{};
    variables_.clear();
    currentCommandIndex_ = 0;
    activeChoices_.clear();
    activeChoiceRects_.clear();
    commandRowRects_.clear();
    commandRowIndices_.clear();
    graphNodeRects_.clear();
    graphNodeIndices_.clear();
    eventRowRects_.clear();
    eventRowIndices_.clear();
    currentEventListRect_ = {};
    toolbarButtonRects_.clear();
    inspectorEditTargets_.clear();
    eventAddTextRect_ = {};
    eventAddChoiceRect_ = {};
    eventDeleteRect_ = {};
    eventDuplicateRect_ = {};
    inspectorCommitRect_ = {};
    inspectorCancelRect_ = {};
    selectedCommandIndex_ = 0;
    selectedChoiceLinkIndex_ = 0;
    previewVisible_ = false;
    leftPanelWidth_ = 280;
    rightPanelWidth_ = 320;
    graphHeight_ = 162;
    eventListHeight_ = 208;
    eventListScrollOffset_ = 0;
    activeDragHandle_ = DragHandle::None;
    waitingForChoice_ = false;
    reachedEnd_ = false;
    InitializeToolbarItems();

    std::wstring scenarioText;
    std::wstring loadedPath;
    if (!requestedPath.empty() && TryReadTextFile(requestedPath, scenarioText))
    {
        loadedPath = requestedPath;
    }
    else
    {
        for (const std::wstring& candidate : GetScenarioCandidates())
        {
            if (TryReadTextFile(candidate, scenarioText))
            {
                loadedPath = candidate;
                break;
            }
        }
    }

    if (loadedPath.empty())
    {
        statusText_ = L"assets/main.ks not found";
        currentText_ = L"Place a scenario file in assets/main.ks.";
        reachedEnd_ = true;
        return;
    }

    ScenarioDocument document;
    std::wstring parseError;
    if (!ParseScenario(scenarioText, document, parseError))
    {
        statusText_ = L"load failed: " + loadedPath;
        currentText_ = parseError;
        reachedEnd_ = true;
        return;
    }

    scenario_ = std::move(document);
    storyTitle_ = scenario_.title;
    statusText_ = L"loaded: " + loadedPath;
    scenarioPath_ = loadedPath;
    scenarioBaseDir_ = GetDirectoryPath(loadedPath);
    projectPath_ = CombinePath(scenarioBaseDir_, L"project.kproj");
    LoadProjectSettings(projectPath_);
    RefreshSceneList();
    RefreshAssetList();
    LoadToolbarIcons();
    selectedCommandIndex_ = 0;
    selectedChoiceLinkIndex_ = 0;
    Advance();
}

void NovelRuntime::Advance()
{
    if (waitingForChoice_)
    {
        return;
    }

    while (currentCommandIndex_ < scenario_.commands.size())
    {
        const ScriptCommand& command = scenario_.commands[currentCommandIndex_++];
        switch (command.type)
        {
        case ScriptCommand::Type::Title:
        {
            const std::wstring title = GetCommandParameter(command, L"name");
            if (!title.empty())
            {
                storyTitle_ = title;
            }
            break;
        }
        case ScriptCommand::Type::Background:
            ApplyBackgroundCommand(command);
            break;
        case ScriptCommand::Type::Character:
            ApplyCharacterCommand(command);
            break;
        case ScriptCommand::Type::HideCharacter:
            ApplyHideCharacterCommand(command);
            break;
        case ScriptCommand::Type::Speaker:
            speakerName_ = GetCommandParameter(command, L"name");
            break;
        case ScriptCommand::Type::ClearSpeaker:
            speakerName_.clear();
            break;
        case ScriptCommand::Type::Text:
            currentText_ = GetCommandParameter(command, L"value");
            if (currentText_.empty())
            {
                currentText_ = L" ";
            }
            return;
        case ScriptCommand::Type::Choice:
            ActivateChoice(command);
            return;
        case ScriptCommand::Type::SetValue:
            ApplySetValueCommand(command);
            break;
        case ScriptCommand::Type::AddValue:
            ApplyAddValueCommand(command);
            break;
        case ScriptCommand::Type::IfJump:
            ApplyIfJumpCommand(command);
            if (reachedEnd_)
            {
                return;
            }
            break;
        case ScriptCommand::Type::Jump:
            if (!JumpToLabel(GetCommandParameter(command, L"target")))
            {
                return;
            }
            break;
        case ScriptCommand::Type::Label:
            break;
        }
    }

    reachedEnd_ = true;
    currentText_ = L"End of script.";
}

bool NovelRuntime::HandleClick(POINT point)
{
    if (HandleToolbarClick(point))
    {
        return true;
    }
    if (HandleSceneClick(point))
    {
        return true;
    }
    if (HandleAssetClick(point))
    {
        return true;
    }
    if (HandleEventActionClick(point))
    {
        return true;
    }
    if (HandleInspectorClick(point))
    {
        return true;
    }
    if (TrySelectCommandFromPoint(point, lastClientRect_))
    {
        return true;
    }

    if (waitingForChoice_)
    {
        for (size_t index = 0; index < activeChoiceRects_.size(); ++index)
        {
            if (PtInRect(&activeChoiceRects_[index], point))
            {
                SelectChoice(index);
                return true;
            }
        }
        return false;
    }

    if (!reachedEnd_)
    {
        Advance();
        return true;
    }

    return false;
}

NovelRuntime::DragHandle NovelRuntime::HitTestDragHandle(POINT point) const
{
    if (PtInRect(&leftSplitterRect_, point))
    {
        return DragHandle::LeftPanel;
    }
    if (PtInRect(&rightSplitterRect_, point))
    {
        return DragHandle::RightPanel;
    }
    if (PtInRect(&graphSplitterRect_, point))
    {
        return DragHandle::GraphHeight;
    }
    if (PtInRect(&eventSplitterRect_, point))
    {
        return DragHandle::EventListHeight;
    }
    return DragHandle::None;
}

bool NovelRuntime::HandleMouseDown(POINT point)
{
    activeDragHandle_ = HitTestDragHandle(point);
    if (activeDragHandle_ != DragHandle::None)
    {
        statusText_ = L"\u5883\u754c\u7dda\u3092\u30c9\u30e9\u30c3\u30b0\u3057\u3066\u30ec\u30a4\u30a2\u30a6\u30c8\u3092\u8abf\u6574\u3067\u304d\u307e\u3059";
        return true;
    }
    return false;
}

bool NovelRuntime::HandleMouseMove(POINT point)
{
    if (activeDragHandle_ == DragHandle::None)
    {
        return false;
    }

    const int clientWidth = lastClientRect_.right - lastClientRect_.left;
    const int clientHeight = lastClientRect_.bottom - lastClientRect_.top;
    if (clientWidth <= 0 || clientHeight <= 0)
    {
        return false;
    }

    switch (activeDragHandle_)
    {
    case DragHandle::LeftPanel:
        leftPanelWidth_ = (std::max)(220, (std::min)(static_cast<int>(point.x), clientWidth - rightPanelWidth_ - 340));
        statusText_ = L"\u30b3\u30f3\u30dd\u30fc\u30cd\u30f3\u30c8\u5e45: " + std::to_wstring(leftPanelWidth_);
        return true;
    case DragHandle::RightPanel:
        rightPanelWidth_ = (std::max)(260, (std::min)(clientWidth - static_cast<int>(point.x), clientWidth - leftPanelWidth_ - 340));
        statusText_ = L"\u30a4\u30f3\u30b9\u30da\u30af\u30bf\u5e45: " + std::to_wstring(rightPanelWidth_);
        return true;
    case DragHandle::GraphHeight:
    {
        const RECT previewRect = GetPreviewRect(lastClientRect_);
        const RECT toolbarRect = GetToolbarRect(previewRect);
        graphHeight_ = (std::max)(120, (std::min)(static_cast<int>(point.y) - (static_cast<int>(toolbarRect.bottom) + 12), 260));
        statusText_ = L"\u30d5\u30ed\u30fc\u9818\u57df: " + std::to_wstring(graphHeight_);
        return true;
    }
    case DragHandle::EventListHeight:
    {
        const RECT previewRect = GetPreviewRect(lastClientRect_);
        const RECT graphRect = GetGraphRect(previewRect);
        eventListHeight_ = (std::max)(140, (std::min)(static_cast<int>(point.y) - (static_cast<int>(graphRect.bottom) + 18), 280));
        statusText_ = L"\u30a4\u30d9\u30f3\u30c8\u4e00\u89a7: " + std::to_wstring(eventListHeight_);
        return true;
    }
    case DragHandle::None:
        break;
    }

    return false;
}

bool NovelRuntime::HandleMouseUp(POINT point)
{
    UNREFERENCED_PARAMETER(point);
    const bool wasDragging = activeDragHandle_ != DragHandle::None;
    activeDragHandle_ = DragHandle::None;
    return wasDragging;
}

bool NovelRuntime::HandleMouseWheel(short delta, POINT point)
{
    if (!showEventList_ || !HasVisibleArea(currentEventListRect_) || !PtInRect(&currentEventListRect_, point))
    {
        return false;
    }

    const int rowHeight = 28;
    const int startY = currentEventListRect_.top + 44;
    const int visibleRows = (currentEventListRect_.bottom - startY - 8) / rowHeight;
    const int total = static_cast<int>(scenario_.commands.size());
    const int maxOffset = (std::max)(0, total - (std::max)(visibleRows, 1));
    const int step = delta > 0 ? -3 : 3;
    eventListScrollOffset_ = (std::max)(0, (std::min)(eventListScrollOffset_ + step, maxOffset));
    return true;
}

ScriptCommand* NovelRuntime::GetSelectedCommand()
{
    if (selectedCommandIndex_ >= scenario_.commands.size())
    {
        return nullptr;
    }
    return &scenario_.commands[selectedCommandIndex_];
}

const ScriptCommand* NovelRuntime::GetSelectedCommand() const
{
    if (selectedCommandIndex_ >= scenario_.commands.size())
    {
        return nullptr;
    }
    return &scenario_.commands[selectedCommandIndex_];
}

void NovelRuntime::SyncDocumentMetadata()
{
    RebuildScenarioLabels(scenario_);
    scenario_.title = storyTitle_;
    for (const ScriptCommand& command : scenario_.commands)
    {
        if (command.type == ScriptCommand::Type::Title)
        {
            const auto found = command.parameters.find(L"name");
            if (found != command.parameters.end() && !found->second.empty())
            {
                scenario_.title = found->second;
                storyTitle_ = found->second;
                break;
            }
        }
    }
}

void NovelRuntime::InsertCommandAfterSelection(ScriptCommand::Type type)
{
    ScriptCommand command;
    command.type = type;
    command.id = L"manual_" + std::to_wstring(scenario_.commands.size() + 1);
    command.sourceLine = selectedCommandIndex_ + 1;

    switch (type)
    {
    case ScriptCommand::Type::Text:
        command.parameters[L"value"] = L"\u65b0\u3057\u3044\u672c\u6587";
        break;
    case ScriptCommand::Type::Choice:
        command.parameters[L"prompt"] = L"\u65b0\u3057\u3044\u9078\u629e\u80a2";
        command.links.push_back({ L"\u9078\u629e\u80a21", L"start" });
        command.links.push_back({ L"\u9078\u629e\u80a22", L"start" });
        break;
    default:
        break;
    }

    const size_t insertIndex = scenario_.commands.empty() ? 0 : (std::min)(selectedCommandIndex_ + 1, scenario_.commands.size());
    scenario_.commands.insert(scenario_.commands.begin() + insertIndex, std::move(command));
    selectedCommandIndex_ = insertIndex;
    SyncDocumentMetadata();
    statusText_ = type == ScriptCommand::Type::Choice ? L"\u9078\u629e\u80a2\u30a4\u30d9\u30f3\u30c8\u3092\u8ffd\u52a0\u3057\u307e\u3057\u305f" : L"\u672c\u6587\u30a4\u30d9\u30f3\u30c8\u3092\u8ffd\u52a0\u3057\u307e\u3057\u305f";
}

void NovelRuntime::DeleteSelectedCommand()
{
    if (scenario_.commands.empty() || selectedCommandIndex_ >= scenario_.commands.size())
    {
        return;
    }

    scenario_.commands.erase(scenario_.commands.begin() + static_cast<std::ptrdiff_t>(selectedCommandIndex_));
    if (selectedCommandIndex_ >= scenario_.commands.size() && !scenario_.commands.empty())
    {
        selectedCommandIndex_ = scenario_.commands.size() - 1;
    }
    if (scenario_.commands.empty())
    {
        selectedCommandIndex_ = 0;
    }
    SyncDocumentMetadata();
    statusText_ = L"\u30a4\u30d9\u30f3\u30c8\u3092\u524a\u9664\u3057\u307e\u3057\u305f";
}

void NovelRuntime::DuplicateSelectedCommand()
{
    const ScriptCommand* selected = GetSelectedCommand();
    if (!selected)
    {
        return;
    }

    ScriptCommand copy = *selected;
    copy.id += L"_copy";
    const size_t insertIndex = selectedCommandIndex_ + 1;
    scenario_.commands.insert(scenario_.commands.begin() + static_cast<std::ptrdiff_t>(insertIndex), std::move(copy));
    selectedCommandIndex_ = insertIndex;
    SyncDocumentMetadata();
    statusText_ = L"\u30a4\u30d9\u30f3\u30c8\u3092\u8907\u88fd\u3057\u307e\u3057\u305f";
}

void NovelRuntime::BeginInspectorEdit(size_t commandIndex, const std::wstring& key, const std::wstring& label, const std::wstring& initialValue)
{
    editingCommandIndex_ = commandIndex;
    editingKey_ = key;
    editingLabel_ = label;
    editingBuffer_ = initialValue;
    inspectorEditing_ = true;
    statusText_ = label + L"\u0020\u3092\u7de8\u96c6\u4e2d\u3067\u3059";
}

void NovelRuntime::CommitInspectorEdit()
{
    if (!inspectorEditing_ || editingCommandIndex_ >= scenario_.commands.size())
    {
        return;
    }

    ScriptCommand& command = scenario_.commands[editingCommandIndex_];
    command.parameters[editingKey_] = editingBuffer_;
    if (command.type == ScriptCommand::Type::Choice && editingKey_ == L"prompt")
    {
        command.parameters[L"prompt"] = editingBuffer_;
    }
    if (command.type == ScriptCommand::Type::Title && editingKey_ == L"name")
    {
        scenario_.title = editingBuffer_;
        storyTitle_ = editingBuffer_;
    }

    SyncDocumentMetadata();
    inspectorEditing_ = false;
    statusText_ = editingLabel_ + L"\u0020\u3092\u66f4\u65b0\u3057\u307e\u3057\u305f";
}

void NovelRuntime::CancelInspectorEdit()
{
    inspectorEditing_ = false;
    editingKey_.clear();
    editingLabel_.clear();
    editingBuffer_.clear();
    statusText_ = L"\u7de8\u96c6\u3092\u30ad\u30e3\u30f3\u30bb\u30eb\u3057\u307e\u3057\u305f";
}

bool NovelRuntime::HandleInspectorClick(POINT point)
{
    if (inspectorEditing_)
    {
        if (PtInRect(&inspectorCommitRect_, point))
        {
            CommitInspectorEdit();
            return true;
        }
        if (PtInRect(&inspectorCancelRect_, point))
        {
            CancelInspectorEdit();
            return true;
        }
    }

    for (const InspectorEditTarget& target : inspectorEditTargets_)
    {
        if (PtInRect(&target.buttonRect, point) && target.commandIndex < scenario_.commands.size())
        {
            const ScriptCommand& command = scenario_.commands[target.commandIndex];
            const auto found = command.parameters.find(target.key);
            BeginInspectorEdit(target.commandIndex, target.key, target.label, found == command.parameters.end() ? L"" : found->second);
            return true;
        }
    }
    return false;
}

bool NovelRuntime::HandleEventActionClick(POINT point)
{
    if (PtInRect(&eventAddTextRect_, point))
    {
        InsertCommandAfterSelection(ScriptCommand::Type::Text);
        return true;
    }
    if (PtInRect(&eventAddChoiceRect_, point))
    {
        InsertCommandAfterSelection(ScriptCommand::Type::Choice);
        return true;
    }
    if (PtInRect(&eventDeleteRect_, point))
    {
        DeleteSelectedCommand();
        return true;
    }
    if (PtInRect(&eventDuplicateRect_, point))
    {
        DuplicateSelectedCommand();
        return true;
    }
    return false;
}

bool NovelRuntime::HandleChar(wchar_t ch)
{
    if (!inspectorEditing_)
    {
        return false;
    }

    if (ch == L'\b')
    {
        if (!editingBuffer_.empty())
        {
            editingBuffer_.pop_back();
        }
        return true;
    }

    if (ch >= 32)
    {
        editingBuffer_.push_back(ch);
        return true;
    }

    return false;
}

void NovelRuntime::LoadProjectSettings(const std::wstring& projectPath)
{
    std::wstring content;
    if (!TryReadTextFile(projectPath, content))
    {
        return;
    }

    std::wistringstream input(content);
    std::wstring line;
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == L'\r')
        {
            line.pop_back();
        }
        const size_t split = line.find(L'=');
        if (split == std::wstring::npos)
        {
            continue;
        }

        const std::wstring key = Trim(line.substr(0, split));
        const std::wstring value = Trim(line.substr(split + 1));
        if (key == L"left_panel_width") leftPanelWidth_ = _wtoi(value.c_str());
        else if (key == L"right_panel_width") rightPanelWidth_ = _wtoi(value.c_str());
        else if (key == L"graph_height") graphHeight_ = _wtoi(value.c_str());
        else if (key == L"event_list_height") eventListHeight_ = _wtoi(value.c_str());
        else if (key == L"show_components") showComponents_ = value == L"1";
        else if (key == L"show_inspector") showInspector_ = value == L"1";
        else if (key == L"show_flow_graph") showFlowGraph_ = value == L"1";
        else if (key == L"show_preview_panel") showPreviewPanel_ = value == L"1";
        else if (key == L"show_event_list") showEventList_ = value == L"1";
    }
}

bool NovelRuntime::SaveProject()
{
    if (scenarioPath_.empty())
    {
        scenarioPath_ = L"assets\\main.ks";
    }
    if (projectPath_.empty())
    {
        const std::wstring baseDir = scenarioBaseDir_.empty() ? L"assets" : scenarioBaseDir_;
        projectPath_ = CombinePath(baseDir, L"project.kproj");
    }

    SyncDocumentMetadata();
    if (!TryWriteTextFile(scenarioPath_, SerializeScenario(scenario_)))
    {
        statusText_ = L"\u30b7\u30ca\u30ea\u30aa\u4fdd\u5b58\u306b\u5931\u6557\u3057\u307e\u3057\u305f";
        return false;
    }

    std::wstring projectText;
    projectText += L"scenario_path=" + scenarioPath_ + L"\r\n";
    projectText += L"left_panel_width=" + std::to_wstring(leftPanelWidth_) + L"\r\n";
    projectText += L"right_panel_width=" + std::to_wstring(rightPanelWidth_) + L"\r\n";
    projectText += L"graph_height=" + std::to_wstring(graphHeight_) + L"\r\n";
    projectText += L"event_list_height=" + std::to_wstring(eventListHeight_) + L"\r\n";
    projectText += L"show_components=" + std::wstring(showComponents_ ? L"1" : L"0") + L"\r\n";
    projectText += L"show_inspector=" + std::wstring(showInspector_ ? L"1" : L"0") + L"\r\n";
    projectText += L"show_flow_graph=" + std::wstring(showFlowGraph_ ? L"1" : L"0") + L"\r\n";
    projectText += L"show_preview_panel=" + std::wstring(showPreviewPanel_ ? L"1" : L"0") + L"\r\n";
    projectText += L"show_event_list=" + std::wstring(showEventList_ ? L"1" : L"0") + L"\r\n";

    if (!TryWriteTextFile(projectPath_, projectText))
    {
        statusText_ = L"\u30d7\u30ed\u30b8\u30a7\u30af\u30c8\u4fdd\u5b58\u306b\u5931\u6557\u3057\u307e\u3057\u305f";
        return false;
    }

    statusText_ = L"\u30d7\u30ed\u30b8\u30a7\u30af\u30c8\u3092\u4fdd\u5b58\u3057\u307e\u3057\u305f";
    return true;
}

bool NovelRuntime::SaveProjectAs()
{
    WCHAR fileBuffer[MAX_PATH] = {};
    const std::wstring defaultName = scenarioPath_.empty() ? L"main.ks" : GetFileNamePart(scenarioPath_);
    wcsncpy_s(fileBuffer, defaultName.c_str(), _TRUNCATE);

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = L"Kaktos Scenario (*.ks)\0*.ks\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"ks";
    if (!GetSaveFileNameW(&ofn))
    {
        statusText_ = L"\u4fdd\u5b58\u3092\u30ad\u30e3\u30f3\u30bb\u30eb\u3057\u307e\u3057\u305f";
        return false;
    }

    scenarioPath_ = fileBuffer;
    scenarioBaseDir_ = GetDirectoryPath(scenarioPath_);
    projectPath_ = CombinePath(scenarioBaseDir_, GetFileStemPart(scenarioPath_) + L".kproj");
    RefreshSceneList();
    RefreshAssetList();
    return SaveProject();
}

void NovelRuntime::RefreshSceneList()
{
    sceneItems_.clear();
    const std::wstring directory = scenarioBaseDir_.empty() ? L"assets" : scenarioBaseDir_;
    for (const std::wstring& path : EnumerateFiles(directory, L"*.ks"))
    {
        sceneItems_.push_back(SceneListItem{ path, GetFileNamePart(path), {} });
    }
}

void NovelRuntime::RefreshAssetList()
{
    assetItems_.clear();
    const std::wstring baseAssetsDir = scenarioBaseDir_.empty() ? L"assets" : scenarioBaseDir_;
    const struct
    {
        const wchar_t* category;
        const wchar_t* subdir;
    } assetDirs[] =
    {
        { L"BG", L"bg" },
        { L"CH", L"ch" },
        { L"BGM", L"bgm" },
        { L"SE", L"se" },
    };

    for (const auto& assetDir : assetDirs)
    {
        const std::wstring dir = CombinePath(baseAssetsDir, assetDir.subdir);
        for (const std::wstring& path : EnumerateFiles(dir, L"*.*"))
        {
            assetItems_.push_back(AssetListItem{ assetDir.category, path, GetFileNamePart(path), {} });
        }
    }
}

bool NovelRuntime::CreateNewScene()
{
    WCHAR fileBuffer[MAX_PATH] = {};
    wcsncpy_s(fileBuffer, L"new_scene.ks", _TRUNCATE);

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"Kaktos Scenario (*.ks)\0*.ks\0";
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"ks";
    if (!GetSaveFileNameW(&ofn))
    {
        return false;
    }

    if (!TryWriteTextFile(fileBuffer, L"[title name=\"New Scene\"]\r\n[text value=\"New Text\"]\r\n"))
    {
        statusText_ = L"\u65b0\u898f\u30b7\u30fc\u30f3\u4f5c\u6210\u306b\u5931\u6557\u3057\u307e\u3057\u305f";
        return false;
    }

    LoadScenario(fileBuffer);
    statusText_ = L"\u65b0\u898f\u30b7\u30fc\u30f3\u3092\u4f5c\u6210\u3057\u307e\u3057\u305f";
    return true;
}

bool NovelRuntime::RenameCurrentScene()
{
    if (scenarioPath_.empty())
    {
        return false;
    }

    WCHAR fileBuffer[MAX_PATH] = {};
    wcsncpy_s(fileBuffer, GetFileNamePart(scenarioPath_).c_str(), _TRUNCATE);
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"Kaktos Scenario (*.ks)\0*.ks\0";
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"ks";
    if (!GetSaveFileNameW(&ofn))
    {
        return false;
    }

    const std::wstring newPath = fileBuffer;
    if (!MoveFileExW(scenarioPath_.c_str(), newPath.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        statusText_ = L"\u30b7\u30fc\u30f3\u540d\u5909\u66f4\u306b\u5931\u6557\u3057\u307e\u3057\u305f";
        return false;
    }

    scenarioPath_ = newPath;
    scenarioBaseDir_ = GetDirectoryPath(newPath);
    projectPath_ = CombinePath(scenarioBaseDir_, GetFileStemPart(newPath) + L".kproj");
    RefreshSceneList();
    statusText_ = L"\u30b7\u30fc\u30f3\u540d\u3092\u5909\u66f4\u3057\u307e\u3057\u305f";
    return true;
}

bool NovelRuntime::HandleSceneClick(POINT point)
{
    if (PtInRect(&sceneAddRect_, point))
    {
        return CreateNewScene();
    }
    if (PtInRect(&sceneRenameRect_, point))
    {
        return RenameCurrentScene();
    }

    for (const SceneListItem& item : sceneItems_)
    {
        if (PtInRect(&item.rect, point))
        {
            LoadScenario(item.path);
            statusText_ = L"\u30b7\u30fc\u30f3\u3092\u5207\u308a\u66ff\u3048\u307e\u3057\u305f";
            return true;
        }
    }
    return false;
}

bool NovelRuntime::HandleAssetClick(POINT point)
{
    ScriptCommand* selected = GetSelectedCommand();
    if (!selected)
    {
        return false;
    }

    for (const AssetListItem& item : assetItems_)
    {
        if (!PtInRect(&item.rect, point))
        {
            continue;
        }

        const std::wstring relative = MakeRelativePath(item.path, scenarioBaseDir_);
        if (item.category == L"BG" && selected->type == ScriptCommand::Type::Background)
        {
            selected->parameters[L"storage"] = relative;
            statusText_ = L"\u80cc\u666f\u753b\u50cf\u3092\u53c2\u7167\u3057\u307e\u3057\u305f";
            return true;
        }
        if (item.category == L"CH" && selected->type == ScriptCommand::Type::Character)
        {
            selected->parameters[L"storage"] = relative;
            statusText_ = L"\u7acb\u3061\u7d75\u753b\u50cf\u3092\u53c2\u7167\u3057\u307e\u3057\u305f";
            return true;
        }
        statusText_ = relative;
        return true;
    }
    return false;
}

void NovelRuntime::ResetLayout()
{
    leftPanelWidth_ = 280;
    rightPanelWidth_ = 320;
    graphHeight_ = 162;
    eventListHeight_ = 208;
    activeDragHandle_ = DragHandle::None;
    statusText_ = L"\u30ec\u30a4\u30a2\u30a6\u30c8\u3092\u521d\u671f\u5316\u3057\u307e\u3057\u305f";
}

bool NovelRuntime::HandleViewMenuCommand(UINT commandId)
{
    switch (commandId)
    {
    case IDM_VIEW_COMPONENTS:
        showComponents_ = !showComponents_;
        statusText_ = showComponents_ ? L"\u30b3\u30f3\u30dd\u30fc\u30cd\u30f3\u30c8\u3092\u8868\u793a" : L"\u30b3\u30f3\u30dd\u30fc\u30cd\u30f3\u30c8\u3092\u975e\u8868\u793a";
        return true;
    case IDM_VIEW_INSPECTOR:
        showInspector_ = !showInspector_;
        statusText_ = showInspector_ ? L"\u30a4\u30f3\u30b9\u30da\u30af\u30bf\u3092\u8868\u793a" : L"\u30a4\u30f3\u30b9\u30da\u30af\u30bf\u3092\u975e\u8868\u793a";
        return true;
    case IDM_VIEW_FLOWGRAPH:
        showFlowGraph_ = !showFlowGraph_;
        statusText_ = showFlowGraph_ ? L"\u30d5\u30ed\u30fc\u30b0\u30e9\u30d5\u3092\u8868\u793a" : L"\u30d5\u30ed\u30fc\u30b0\u30e9\u30d5\u3092\u975e\u8868\u793a";
        return true;
    case IDM_VIEW_PREVIEW:
        showPreviewPanel_ = !showPreviewPanel_;
        statusText_ = showPreviewPanel_ ? L"\u30d7\u30ec\u30d3\u30e5\u30fc\u9818\u57df\u3092\u8868\u793a" : L"\u30d7\u30ec\u30d3\u30e5\u30fc\u9818\u57df\u3092\u975e\u8868\u793a";
        return true;
    case IDM_VIEW_EVENTLIST:
        showEventList_ = !showEventList_;
        statusText_ = showEventList_ ? L"\u30a4\u30d9\u30f3\u30c8\u4e00\u89a7\u3092\u8868\u793a" : L"\u30a4\u30d9\u30f3\u30c8\u4e00\u89a7\u3092\u975e\u8868\u793a";
        return true;
    case IDM_VIEW_RESET_LAYOUT:
        ResetLayout();
        return true;
    default:
        return false;
    }
}

bool NovelRuntime::IsViewMenuChecked(UINT commandId) const
{
    switch (commandId)
    {
    case IDM_VIEW_COMPONENTS: return showComponents_;
    case IDM_VIEW_INSPECTOR: return showInspector_;
    case IDM_VIEW_FLOWGRAPH: return showFlowGraph_;
    case IDM_VIEW_PREVIEW: return showPreviewPanel_;
    case IDM_VIEW_EVENTLIST: return showEventList_;
    default: return false;
    }
}

bool NovelRuntime::HandleKeyDown(WPARAM key)
{
    if (inspectorEditing_)
    {
        if (key == VK_RETURN)
        {
            CommitInspectorEdit();
            return true;
        }
        if (key == VK_ESCAPE)
        {
            CancelInspectorEdit();
            return true;
        }
        return false;
    }

    if (waitingForChoice_)
    {
        if (key >= '1' && key < '1' + static_cast<WPARAM>(activeChoices_.size()))
        {
            SelectChoice(static_cast<size_t>(key - '1'));
            return true;
        }
        return false;
    }

    if (!scenario_.commands.empty() &&
        selectedCommandIndex_ < scenario_.commands.size() &&
        scenario_.commands[selectedCommandIndex_].type == ScriptCommand::Type::Choice &&
        key >= '1' && key <= '9')
    {
        const size_t requestedIndex = static_cast<size_t>(key - '1');
        if (requestedIndex < scenario_.commands[selectedCommandIndex_].links.size())
        {
            selectedChoiceLinkIndex_ = requestedIndex;
            statusText_ = L"choice link selected: " + std::to_wstring(requestedIndex + 1);
            return true;
        }
    }

    if (key == VK_RETURN || key == VK_SPACE || key == VK_RIGHT)
    {
        if (!reachedEnd_)
        {
            Advance();
            return true;
        }
    }

    return false;
}

bool NovelRuntime::IsEditableSourceNode(size_t commandIndex) const
{
    if (commandIndex >= scenario_.commands.size())
    {
        return false;
    }

    const ScriptCommand::Type type = scenario_.commands[commandIndex].type;
    return type == ScriptCommand::Type::Choice ||
        type == ScriptCommand::Type::Jump ||
        type == ScriptCommand::Type::IfJump;
}

bool NovelRuntime::IsLabelNode(size_t commandIndex) const
{
    return commandIndex < scenario_.commands.size() &&
        scenario_.commands[commandIndex].type == ScriptCommand::Type::Label;
}

void NovelRuntime::RewireSelectedSourceToLabel(size_t labelCommandIndex)
{
    if (selectedCommandIndex_ >= scenario_.commands.size() || !IsLabelNode(labelCommandIndex))
    {
        return;
    }

    const std::wstring labelName = GetCommandParameter(scenario_.commands[labelCommandIndex], L"name");
    ScriptCommand& selectedCommand = scenario_.commands[selectedCommandIndex_];

    if (selectedCommand.type == ScriptCommand::Type::Choice)
    {
        if (selectedChoiceLinkIndex_ < selectedCommand.links.size())
        {
            selectedCommand.links[selectedChoiceLinkIndex_].second = labelName;
            statusText_ = L"choice link rewired to: " + labelName;
        }
        return;
    }

    selectedCommand.parameters[L"target"] = labelName;
    statusText_ = L"target rewired to: " + labelName;
}

bool NovelRuntime::HandleGraphNodeSelection(size_t commandIndex)
{
    if (IsLabelNode(commandIndex) && IsEditableSourceNode(selectedCommandIndex_))
    {
        RewireSelectedSourceToLabel(commandIndex);
        return true;
    }

    selectedCommandIndex_ = commandIndex;
    if (selectedCommandIndex_ < scenario_.commands.size() &&
        scenario_.commands[selectedCommandIndex_].type == ScriptCommand::Type::Choice)
    {
        selectedChoiceLinkIndex_ = 0;
    }
    return true;
}

void NovelRuntime::DrawWrappedText(HDC hdc, const RECT& bounds, const std::wstring& text, UINT format) const
{
    RECT rect = bounds;
    DrawTextW(hdc, text.c_str(), -1, &rect, format);
}

void NovelRuntime::DrawCharacterSlot(HDC hdc, const RECT& stageRect, CharacterSlot& slot, int centerX) const
{
    if (slot.displayName.empty() && !slot.image)
    {
        return;
    }

    const int slotWidth = 260;
    const int slotHeight = 520;
    const RECT characterRect = { centerX - slotWidth / 2, stageRect.bottom - slotHeight - 20, centerX + slotWidth / 2, stageRect.bottom - 20 };

    if (slot.image)
    {
        Gdiplus::Graphics graphics(hdc);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.DrawImage(slot.image.get(), Gdiplus::Rect(characterRect.left, characterRect.top, characterRect.right - characterRect.left, characterRect.bottom - characterRect.top));
    }
    else
    {
        HBRUSH fillBrush = CreateSolidBrush(RGB(74, 91, 118));
        FillRect(hdc, &characterRect, fillBrush);
        DeleteObject(fillBrush);
        FrameRect(hdc, &characterRect, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(230, 236, 242));
        RECT labelRect = characterRect;
        labelRect.left += 18;
        labelRect.top += 18;
        labelRect.right -= 18;
        DrawTextW(hdc, slot.displayName.c_str(), -1, &labelRect, DT_CENTER | DT_TOP | DT_WORDBREAK);
    }

    RECT plateRect = { characterRect.left + 24, characterRect.bottom - 52, characterRect.right - 24, characterRect.bottom - 12 };
    HBRUSH plateBrush = CreateSolidBrush(RGB(10, 12, 16));
    FillRect(hdc, &plateRect, plateBrush);
    DeleteObject(plateBrush);
    SetTextColor(hdc, RGB(255, 240, 190));
    DrawTextW(hdc, slot.displayName.c_str(), -1, &plateRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void NovelRuntime::DrawChoices(HDC hdc, const RECT& messageRect)
{
    activeChoiceRects_.assign(activeChoices_.size(), RECT{});
    if (activeChoices_.empty())
    {
        return;
    }

    const int buttonTop = messageRect.top - static_cast<int>(activeChoices_.size()) * 56 - 18;
    for (size_t index = 0; index < activeChoices_.size(); ++index)
    {
        RECT optionRect = { messageRect.left + 64, buttonTop + static_cast<int>(index) * 56, messageRect.right - 64, buttonTop + static_cast<int>(index) * 56 + 42 };
        activeChoiceRects_[index] = optionRect;

        HBRUSH optionBrush = CreateSolidBrush(RGB(32, 40, 56));
        FillRect(hdc, &optionRect, optionBrush);
        DeleteObject(optionBrush);
        FrameRect(hdc, &optionRect, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));

        RECT textRect = optionRect;
        textRect.left += 16;
        textRect.right -= 16;
        SetTextColor(hdc, RGB(238, 242, 247));
        const std::wstring label = std::to_wstring(index + 1) + L". " + activeChoices_[index].first;
        DrawTextW(hdc, label.c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
}

bool NovelRuntime::HandleToolbarClick(POINT point)
{
    for (size_t i = 0; i < toolbarButtonRects_.size() && i < toolbarItems_.size(); ++i)
    {
        if (!PtInRect(&toolbarButtonRects_[i], point))
        {
            continue;
        }

        const ToolbarItem& item = toolbarItems_[i];
        if (item.id == L"preview")
        {
            previewVisible_ = !previewVisible_;
            statusText_ = previewVisible_ ? L"\u30d7\u30ec\u30d3\u30e5\u30fc\u3092\u8868\u793a\u3057\u307e\u3057\u305f" : L"\u30d7\u30ec\u30d3\u30e5\u30fc\u3092\u9589\u3058\u307e\u3057\u305f";
            return true;
        }
        if (item.id == L"save")
        {
            SaveProjectAs();
            return true;
        }

        statusText_ = item.label + L"\u0020\u306f\u3053\u308c\u304b\u3089\u5b9f\u88c5\u3057\u307e\u3059";
        return true;
    }

    return false;
}

void NovelRuntime::DrawSplitter(HDC hdc, const RECT& rect, bool active) const
{
    HBRUSH brush = CreateSolidBrush(active ? RGB(110, 176, 214) : RGB(48, 58, 72));
    FillRect(hdc, &rect, brush);
    DeleteObject(brush);
}

void NovelRuntime::DrawToolbar(HDC hdc, const RECT& previewRect)
{
    toolbarButtonRects_.clear();

    const RECT toolbarRect = GetToolbarRect(previewRect);
    HBRUSH toolbarBrush = CreateSolidBrush(RGB(22, 30, 40));
    FillRect(hdc, &toolbarRect, toolbarBrush);
    DeleteObject(toolbarBrush);
    FrameRect(hdc, &toolbarRect, static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));

    SetBkMode(hdc, TRANSPARENT);
    HFONT titleFont = CreateFontW(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
    HFONT buttonFont = CreateFontW(16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
    HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, titleFont));

    RECT titleRect = { toolbarRect.left + 18, toolbarRect.top + 2, toolbarRect.left + 320, toolbarRect.bottom - 2 };
    SetTextColor(hdc, RGB(240, 244, 248));
    DrawWrappedText(hdc, titleRect, L"\u30d3\u30eb\u30c0\u30fc\u30ef\u30fc\u30af\u30b9\u30da\u30fc\u30b9", DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    SelectObject(hdc, buttonFont);
    Gdiplus::Graphics graphics(hdc);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);

    int buttonRight = toolbarRect.right - 12;
    const int buttonWidth = 112;
    const int buttonHeight = 34;
    for (size_t index = toolbarItems_.size(); index-- > 0;)
    {
        const ToolbarItem& item = toolbarItems_[index];
        RECT buttonRect = {
            buttonRight - buttonWidth,
            toolbarRect.top + 8,
            buttonRight,
            toolbarRect.top + 8 + buttonHeight
        };
        toolbarButtonRects_.insert(toolbarButtonRects_.begin(), buttonRect);
        buttonRight -= buttonWidth + 8;

        const bool active = item.id == L"preview" && previewVisible_;
        HBRUSH buttonBrush = CreateSolidBrush(active ? RGB(194, 226, 245) : RGB(38, 52, 68));
        FillRect(hdc, &buttonRect, buttonBrush);
        DeleteObject(buttonBrush);

        HPEN borderPen = CreatePen(PS_SOLID, 1, active ? RGB(120, 176, 208) : RGB(72, 94, 116));
        HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, borderPen));
        HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(HOLLOW_BRUSH)));
        Rectangle(hdc, buttonRect.left, buttonRect.top, buttonRect.right, buttonRect.bottom);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(borderPen);

        int textLeft = buttonRect.left + 8;
        if (item.iconImage)
        {
            const int iconSize = 18;
            const int iconTop = buttonRect.top + (buttonHeight - iconSize) / 2;
            graphics.DrawImage(item.iconImage.get(), Gdiplus::Rect(buttonRect.left + 8, iconTop, iconSize, iconSize));
            textLeft += 24;
        }

        RECT textRect = { textLeft, buttonRect.top, buttonRect.right - 6, buttonRect.bottom };
        SetTextColor(hdc, active ? RGB(38, 68, 92) : RGB(238, 244, 250));
        DrawWrappedText(hdc, textRect, item.label, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }

    SelectObject(hdc, oldFont);
    DeleteObject(titleFont);
    DeleteObject(buttonFont);
}

void NovelRuntime::Draw(HDC hdc, const RECT& clientRect)
{
    lastClientRect_ = clientRect;
    commandRowRects_.clear();
    commandRowIndices_.clear();
    graphNodeRects_.clear();
    graphNodeIndices_.clear();
    eventRowRects_.clear();
    eventRowIndices_.clear();
    toolbarButtonRects_.clear();

    HBRUSH backgroundBrush = CreateSolidBrush(RGB(10, 14, 18));
    FillRect(hdc, &clientRect, backgroundBrush);
    DeleteObject(backgroundBrush);

    const RECT leftPanelRect = GetLeftPanelRect(clientRect);
    const RECT rightPanelRect = GetRightPanelRect(clientRect);
    const RECT previewRect = GetPreviewRect(clientRect);
    const RECT graphRect = GetGraphRect(previewRect);
    const RECT eventRect = GetEventListRect(previewRect);

    HBRUSH leftBrush = CreateSolidBrush(RGB(18, 24, 32));
    HBRUSH rightBrush = CreateSolidBrush(RGB(18, 24, 32));
    HBRUSH centerBrush = CreateSolidBrush(RGB(14, 20, 28));
    if (HasVisibleArea(leftPanelRect))
    {
        FillRect(hdc, &leftPanelRect, leftBrush);
    }
    if (HasVisibleArea(rightPanelRect))
    {
        FillRect(hdc, &rightPanelRect, rightBrush);
    }
    if (HasVisibleArea(previewRect))
    {
        FillRect(hdc, &previewRect, centerBrush);
    }
    DeleteObject(leftBrush);
    DeleteObject(rightBrush);
    DeleteObject(centerBrush);

    leftSplitterRect_ = showComponents_ ? RECT{ leftPanelRect.right, clientRect.top + 12, leftPanelRect.right + 6, clientRect.bottom - 12 } : RECT{};
    rightSplitterRect_ = showInspector_ ? RECT{ rightPanelRect.left - 6, clientRect.top + 12, rightPanelRect.left, clientRect.bottom - 12 } : RECT{};
    graphSplitterRect_ = showFlowGraph_ ? RECT{ graphRect.left, graphRect.bottom + 6, graphRect.right, graphRect.bottom + 10 } : RECT{};
    eventSplitterRect_ = showEventList_ ? RECT{ eventRect.left, eventRect.bottom + 6, eventRect.right, eventRect.bottom + 10 } : RECT{};

    if (showComponents_ && HasVisibleArea(leftPanelRect))
    {
        DrawCommandPalette(hdc, leftPanelRect);
        DrawSplitter(hdc, leftSplitterRect_, activeDragHandle_ == DragHandle::LeftPanel);
    }
    if (showInspector_ && HasVisibleArea(rightPanelRect))
    {
        DrawInspector(hdc, rightPanelRect);
        DrawSplitter(hdc, rightSplitterRect_, activeDragHandle_ == DragHandle::RightPanel);
    }
    if (HasVisibleArea(previewRect))
    {
        DrawToolbar(hdc, previewRect);
        if (showFlowGraph_ && HasVisibleArea(graphRect))
        {
            DrawNodeGraph(hdc, graphRect);
            DrawSplitter(hdc, graphSplitterRect_, activeDragHandle_ == DragHandle::GraphHeight);
        }
        if (showEventList_ && HasVisibleArea(eventRect))
        {
            DrawEventList(hdc, eventRect);
            DrawSplitter(hdc, eventSplitterRect_, activeDragHandle_ == DragHandle::EventListHeight);
        }
    }

    RECT stageRect = GetStageRect(previewRect);
    RECT messageRect = GetMessageRect(previewRect);

    SetBkMode(hdc, TRANSPARENT);
    HFONT titleFont = CreateFontW(28, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
    HFONT bodyFont = CreateFontW(24, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
    HFONT speakerFont = CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
    HFONT hintFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
    HFONT originalFont = static_cast<HFONT>(SelectObject(hdc, titleFont));

    if (showPreviewPanel_ && previewVisible_ && HasVisibleArea(stageRect) && HasVisibleArea(messageRect))
    {
        HBRUSH stageBrush = CreateSolidBrush(backgroundColor_);
        FillRect(hdc, &stageRect, stageBrush);
        DeleteObject(stageBrush);

        if (backgroundImage_)
        {
            Gdiplus::Graphics graphics(hdc);
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            graphics.DrawImage(backgroundImage_.get(), Gdiplus::Rect(stageRect.left, stageRect.top, stageRect.right - stageRect.left, stageRect.bottom - stageRect.top));
        }

        HBRUSH shadeBrush = CreateSolidBrush(RGB(0, 0, 0));
        BLENDFUNCTION blend = { AC_SRC_OVER, 0, 45, 0 };
        HDC memoryDc = CreateCompatibleDC(hdc);
        HBITMAP overlayBitmap = CreateCompatibleBitmap(hdc, stageRect.right - stageRect.left, stageRect.bottom - stageRect.top);
        HGDIOBJ originalBitmap = SelectObject(memoryDc, overlayBitmap);
        RECT overlayRect = { 0, 0, stageRect.right - stageRect.left, stageRect.bottom - stageRect.top };
        FillRect(memoryDc, &overlayRect, shadeBrush);
        AlphaBlend(hdc, stageRect.left, stageRect.top, stageRect.right - stageRect.left, stageRect.bottom - stageRect.top, memoryDc, 0, 0, stageRect.right - stageRect.left, stageRect.bottom - stageRect.top, blend);
        SelectObject(memoryDc, originalBitmap);
        DeleteObject(overlayBitmap);
        DeleteDC(memoryDc);
        DeleteObject(shadeBrush);

        const int stageWidth = stageRect.right - stageRect.left;
        DrawCharacterSlot(hdc, stageRect, leftCharacter_, stageRect.left + stageWidth / 4);
        DrawCharacterSlot(hdc, stageRect, centerCharacter_, stageRect.left + stageWidth / 2);
        DrawCharacterSlot(hdc, stageRect, rightCharacter_, stageRect.left + (stageWidth * 3) / 4);

        SetTextColor(hdc, RGB(220, 228, 236));
        RECT titleRect = { stageRect.left + 12, stageRect.top + 12, stageRect.right - 12, stageRect.top + 52 };
        DrawWrappedText(hdc, titleRect, storyTitle_, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

        HBRUSH messageBrush = CreateSolidBrush(RGB(8, 10, 14));
        FillRect(hdc, &messageRect, messageBrush);
        DeleteObject(messageBrush);
        FrameRect(hdc, &messageRect, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));

        DrawChoices(hdc, messageRect);

        if (!speakerName_.empty())
        {
            SelectObject(hdc, speakerFont);
            RECT nameRect = { messageRect.left + 24, messageRect.top + 18, messageRect.right - 24, messageRect.top + 56 };
            SetTextColor(hdc, RGB(123, 203, 255));
            DrawWrappedText(hdc, nameRect, speakerName_, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        }

        SelectObject(hdc, bodyFont);
        SetTextColor(hdc, RGB(242, 244, 247));
        RECT bodyRect = { messageRect.left + 24, messageRect.top + 64, messageRect.right - 24, messageRect.bottom - 42 };
        DrawWrappedText(hdc, bodyRect, currentText_, DT_LEFT | DT_WORDBREAK);

        SelectObject(hdc, hintFont);
        SetTextColor(hdc, RGB(160, 170, 180));
        RECT hintRect = { messageRect.left + 24, messageRect.bottom - 30, messageRect.right - 24, messageRect.bottom - 10 };
        const std::wstring hintText = waitingForChoice_ ? L"\u30af\u30ea\u30c3\u30af\u307e\u305f\u306f 1-9 \u30ad\u30fc\u3067\u9078\u629e" : (reachedEnd_ ? L"\u30b9\u30af\u30ea\u30d7\u30c8\u7d42\u7aef" : L"\u30af\u30ea\u30c3\u30af / Enter / Space \u3067\u9032\u884c");
        DrawWrappedText(hdc, hintRect, hintText, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);

        if (!statusText_.empty())
        {
            RECT statusRect = { stageRect.left + 12, stageRect.bottom - 42, stageRect.right - 12, stageRect.bottom - 12 };
            DrawWrappedText(hdc, statusRect, statusText_, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        }
    }
    else if (showPreviewPanel_ && HasVisibleArea(stageRect) && HasVisibleArea(messageRect))
    {
        HBRUSH hiddenBrush = CreateSolidBrush(RGB(20, 28, 38));
        FillRect(hdc, &stageRect, hiddenBrush);
        DeleteObject(hiddenBrush);
        FrameRect(hdc, &stageRect, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));

        RECT hiddenTitleRect = { stageRect.left + 24, stageRect.top + 28, stageRect.right - 24, stageRect.top + 70 };
        SetTextColor(hdc, RGB(226, 234, 242));
        DrawWrappedText(hdc, hiddenTitleRect, L"\u30d7\u30ec\u30d3\u30e5\u30fc\u306f\u975e\u8868\u793a\u3067\u3059", DT_CENTER | DT_SINGLELINE | DT_VCENTER);

        SelectObject(hdc, bodyFont);
        RECT hiddenBodyRect = { stageRect.left + 48, stageRect.top + 92, stageRect.right - 48, stageRect.top + 170 };
        SetTextColor(hdc, RGB(154, 164, 176));
        DrawWrappedText(hdc, hiddenBodyRect, L"\u53f3\u4e0a\u30c4\u30fc\u30eb\u30d0\u30fc\u306e\u300c\u30d7\u30ec\u30d3\u30e5\u30fc\u300d\u3092\u62bc\u3059\u3068\u5b9f\u884c\u753b\u9762\u3092\u958b\u3051\u307e\u3059\u3002\u5883\u754c\u7dda\u3092\u30c9\u30e9\u30c3\u30b0\u3059\u308b\u3068\u5404\u30da\u30a4\u30f3\u306e\u30b5\u30a4\u30ba\u3082\u5909\u3048\u3089\u308c\u307e\u3059\u3002", DT_CENTER | DT_WORDBREAK);

        HBRUSH statusBrush = CreateSolidBrush(RGB(14, 18, 24));
        FillRect(hdc, &messageRect, statusBrush);
        DeleteObject(statusBrush);
        FrameRect(hdc, &messageRect, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));

        RECT stateRect = { messageRect.left + 20, messageRect.top + 18, messageRect.right - 20, messageRect.top + 50 };
        SetTextColor(hdc, RGB(196, 206, 216));
        DrawWrappedText(hdc, stateRect, L"\u73fe\u5728\u306e\u672c\u6587", DT_LEFT | DT_SINGLELINE | DT_VCENTER);

        SelectObject(hdc, bodyFont);
        RECT textRect = { messageRect.left + 20, messageRect.top + 56, messageRect.right - 20, messageRect.bottom - 18 };
        SetTextColor(hdc, RGB(170, 178, 188));
        DrawWrappedText(hdc, textRect, currentText_, DT_LEFT | DT_WORDBREAK);
    }

    SelectObject(hdc, originalFont);
    DeleteObject(titleFont);
    DeleteObject(bodyFont);
    DeleteObject(speakerFont);
    DeleteObject(hintFont);
}

COLORREF NovelRuntime::GetCommandAccentColor(const ScriptCommand& command) const
{
    switch (command.type)
    {
    case ScriptCommand::Type::Text: return RGB(239, 134, 143);
    case ScriptCommand::Type::Choice: return RGB(241, 120, 114);
    case ScriptCommand::Type::Jump: return RGB(241, 120, 114);
    case ScriptCommand::Type::Label: return RGB(241, 120, 114);
    case ScriptCommand::Type::Background: return RGB(120, 201, 63);
    case ScriptCommand::Type::Character: return RGB(241, 172, 59);
    case ScriptCommand::Type::HideCharacter: return RGB(205, 205, 205);
    case ScriptCommand::Type::Speaker: return RGB(170, 135, 232);
    case ScriptCommand::Type::ClearSpeaker: return RGB(170, 135, 232);
    case ScriptCommand::Type::SetValue: return RGB(255, 160, 77);
    case ScriptCommand::Type::AddValue: return RGB(255, 160, 77);
    case ScriptCommand::Type::IfJump: return RGB(80, 158, 238);
    case ScriptCommand::Type::Title: return RGB(120, 120, 120);
    }
    return RGB(150, 150, 150);
}

void NovelRuntime::DrawCommandPalette(HDC hdc, const RECT& panelRect)
{
    SetBkMode(hdc, TRANSPARENT);
    HFONT titleFont = CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
    HFONT bodyFont = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
    HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, titleFont));

    sceneAddRect_ = {};
    sceneRenameRect_ = {};
    for (SceneListItem& item : sceneItems_)
    {
        item.rect = {};
    }
    for (AssetListItem& item : assetItems_)
    {
        item.rect = {};
    }

    HBRUSH panelBrush = CreateSolidBrush(RGB(18, 24, 32));
    FillRect(hdc, &panelRect, panelBrush);
    DeleteObject(panelBrush);

    RECT headerRect = { panelRect.left + 16, panelRect.top + 12, panelRect.right - 16, panelRect.top + 36 };
    SetTextColor(hdc, RGB(230, 236, 242));
    DrawWrappedText(hdc, headerRect, L"\u30b3\u30f3\u30dd\u30fc\u30cd\u30f3\u30c8", DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    SelectObject(hdc, bodyFont);
    int cursorY = panelRect.top + 50;
    RECT sceneBand = { panelRect.left + 12, cursorY, panelRect.right - 12, cursorY + 26 };
    HBRUSH sceneBandBrush = CreateSolidBrush(RGB(72, 102, 144));
    FillRect(hdc, &sceneBand, sceneBandBrush);
    DeleteObject(sceneBandBrush);
    SetTextColor(hdc, RGB(255, 255, 255));
    DrawWrappedText(hdc, sceneBand, L"\u30b7\u30fc\u30f3\u7ba1\u7406", DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    cursorY += 34;

    sceneAddRect_ = { panelRect.left + 16, cursorY, panelRect.left + 88, cursorY + 28 };
    sceneRenameRect_ = { panelRect.left + 96, cursorY, panelRect.left + 184, cursorY + 28 };
    HBRUSH addBrush = CreateSolidBrush(RGB(58, 98, 68));
    FillRect(hdc, &sceneAddRect_, addBrush);
    DeleteObject(addBrush);
    HBRUSH renameBrush = CreateSolidBrush(RGB(76, 86, 110));
    FillRect(hdc, &sceneRenameRect_, renameBrush);
    DeleteObject(renameBrush);
    FrameRect(hdc, &sceneAddRect_, static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));
    FrameRect(hdc, &sceneRenameRect_, static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));
    DrawWrappedText(hdc, sceneAddRect_, L"\u65b0\u898f", DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    DrawWrappedText(hdc, sceneRenameRect_, L"\u540d\u524d\u5909\u66f4", DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    cursorY += 38;

    for (SceneListItem& item : sceneItems_)
    {
        item.rect = { panelRect.left + 16, cursorY, panelRect.right - 16, cursorY + 26 };
        const bool isCurrent = item.path == scenarioPath_;
        HBRUSH itemBrush = CreateSolidBrush(isCurrent ? RGB(34, 52, 78) : RGB(29, 37, 48));
        FillRect(hdc, &item.rect, itemBrush);
        DeleteObject(itemBrush);
        FrameRect(hdc, &item.rect, static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));
        SetTextColor(hdc, isCurrent ? RGB(255, 225, 160) : RGB(212, 220, 228));
        DrawWrappedText(hdc, item.rect, item.label, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        cursorY += 30;
    }

    cursorY += 8;
    const struct
    {
        const wchar_t* title;
        COLORREF color;
        const wchar_t* id;
    } assetBands[] =
    {
        { L"\u80cc\u666f", RGB(84, 143, 60), L"BG" },
        { L"\u7acb\u3061\u7d75", RGB(176, 128, 54), L"CH" },
        { L"BGM", RGB(112, 94, 178), L"BGM" },
        { L"SE", RGB(66, 121, 186), L"SE" },
    };

    for (const auto& band : assetBands)
    {
        RECT titleBand = { panelRect.left + 12, cursorY, panelRect.right - 12, cursorY + 24 };
        HBRUSH titleBrush = CreateSolidBrush(band.color);
        FillRect(hdc, &titleBand, titleBrush);
        DeleteObject(titleBrush);
        SetTextColor(hdc, RGB(255, 255, 255));
        DrawWrappedText(hdc, titleBand, band.title, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        cursorY += 28;

        int shown = 0;
        for (AssetListItem& item : assetItems_)
        {
            if (item.category != band.id || shown >= 4)
            {
                continue;
            }

            item.rect = { panelRect.left + 16, cursorY, panelRect.right - 16, cursorY + 24 };
            HBRUSH itemBrush = CreateSolidBrush(RGB(29, 37, 48));
            FillRect(hdc, &item.rect, itemBrush);
            DeleteObject(itemBrush);
            FrameRect(hdc, &item.rect, static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));
            SetTextColor(hdc, RGB(212, 220, 228));
            DrawWrappedText(hdc, item.rect, item.label, DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE | DT_VCENTER);
            cursorY += 28;
            ++shown;
        }

        if (shown == 0)
        {
            RECT emptyRect = { panelRect.left + 16, cursorY, panelRect.right - 16, cursorY + 24 };
            SetTextColor(hdc, RGB(124, 136, 148));
            DrawWrappedText(hdc, emptyRect, L"(empty)", DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            cursorY += 28;
        }

        cursorY += 6;
    }

    SelectObject(hdc, oldFont);
    DeleteObject(titleFont);
    DeleteObject(bodyFont);
}

void NovelRuntime::DrawNodeGraph(HDC hdc, const RECT& panelRect)
{
    HBRUSH backgroundBrush = CreateSolidBrush(RGB(16, 20, 28));
    FillRect(hdc, &panelRect, backgroundBrush);
    DeleteObject(backgroundBrush);
    FrameRect(hdc, &panelRect, static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));

    SetBkMode(hdc, TRANSPARENT);
    HFONT headerFont = CreateFontW(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
    HFONT nodeFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
    HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, headerFont));

    RECT titleRect = { panelRect.left + 16, panelRect.top + 12, panelRect.right - 16, panelRect.top + 40 };
    SetTextColor(hdc, RGB(245, 247, 250));
    DrawWrappedText(hdc, titleRect, L"\u30d5\u30ed\u30fc\u30b0\u30e9\u30d5", DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    SelectObject(hdc, nodeFont);
    const int topY = panelRect.top + 56;
    const int labelY = panelRect.top + 150;
    const int nodeWidth = 170;
    const int nodeHeight = 52;
    const int labelWidth = 150;
    const int labelHeight = 44;
    const int leftMargin = panelRect.left + 20;
    const int horizontalGap = 24;

    std::unordered_map<size_t, RECT> nodeRects;
    std::unordered_map<std::wstring, RECT> labelRects;
    std::vector<size_t> sourceIndices;
    std::vector<size_t> labelIndices;

    for (size_t i = 0; i < scenario_.commands.size(); ++i)
    {
        const ScriptCommand& command = scenario_.commands[i];
        if (command.type == ScriptCommand::Type::Choice || command.type == ScriptCommand::Type::Jump || command.type == ScriptCommand::Type::IfJump)
        {
            sourceIndices.push_back(i);
        }
        if (command.type == ScriptCommand::Type::Label)
        {
            labelIndices.push_back(i);
        }
    }

    for (size_t i = 0; i < sourceIndices.size(); ++i)
    {
        RECT rect = {
            leftMargin + static_cast<int>(i) * (nodeWidth + horizontalGap),
            topY,
            leftMargin + static_cast<int>(i) * (nodeWidth + horizontalGap) + nodeWidth,
            topY + nodeHeight
        };
        nodeRects[sourceIndices[i]] = rect;
        graphNodeRects_.push_back(rect);
        graphNodeIndices_.push_back(sourceIndices[i]);
    }

    for (size_t i = 0; i < labelIndices.size(); ++i)
    {
        RECT rect = {
            leftMargin + static_cast<int>(i) * (labelWidth + horizontalGap),
            labelY,
            leftMargin + static_cast<int>(i) * (labelWidth + horizontalGap) + labelWidth,
            labelY + labelHeight
        };
        const std::wstring name = GetCommandParameter(scenario_.commands[labelIndices[i]], L"name");
        labelRects[name] = rect;
        graphNodeRects_.push_back(rect);
        graphNodeIndices_.push_back(labelIndices[i]);
    }

    HPEN linePen = CreatePen(PS_SOLID, 2, RGB(120, 190, 255));
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, linePen));
    for (size_t sourceIndex : sourceIndices)
    {
        const ScriptCommand& command = scenario_.commands[sourceIndex];
        RECT fromRect = nodeRects[sourceIndex];
        POINT from = { (fromRect.left + fromRect.right) / 2, fromRect.bottom };

        if (command.type == ScriptCommand::Type::Choice)
        {
            for (size_t linkIndex = 0; linkIndex < command.links.size(); ++linkIndex)
            {
                const auto& link = command.links[linkIndex];
                const auto found = labelRects.find(link.second);
                if (found == labelRects.end())
                {
                    continue;
                }

                RECT toRect = found->second;
                HPEN choicePen = CreatePen(
                    PS_SOLID,
                    sourceIndex == selectedCommandIndex_ && linkIndex == selectedChoiceLinkIndex_ ? 3 : 2,
                    sourceIndex == selectedCommandIndex_ && linkIndex == selectedChoiceLinkIndex_ ? RGB(255, 215, 120) : RGB(120, 190, 255));
                HPEN previousPen = static_cast<HPEN>(SelectObject(hdc, choicePen));
                MoveToEx(hdc, from.x, from.y, nullptr);
                LineTo(hdc, (toRect.left + toRect.right) / 2, toRect.top);
                SelectObject(hdc, previousPen);
                DeleteObject(choicePen);
            }
        }
        else
        {
            const std::wstring target = GetCommandParameter(command, L"target");
            const auto found = labelRects.find(target);
            if (found != labelRects.end())
            {
                RECT toRect = found->second;
                MoveToEx(hdc, from.x, from.y, nullptr);
                LineTo(hdc, (toRect.left + toRect.right) / 2, toRect.top);
            }
        }
    }
    SelectObject(hdc, oldPen);
    DeleteObject(linePen);

    for (size_t sourceIndex : sourceIndices)
    {
        const ScriptCommand& command = scenario_.commands[sourceIndex];
        RECT rect = nodeRects[sourceIndex];
        COLORREF fill = sourceIndex == selectedCommandIndex_ ? RGB(84, 105, 70) : RGB(38, 48, 64);
        HBRUSH brush = CreateSolidBrush(fill);
        FillRect(hdc, &rect, brush);
        DeleteObject(brush);
        FrameRect(hdc, &rect, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));

        RECT typeRect = { rect.left + 8, rect.top + 6, rect.right - 8, rect.top + 24 };
        SetTextColor(hdc, RGB(255, 225, 160));
        DrawWrappedText(hdc, typeRect, GetCommandTypeLabel(command), DT_LEFT | DT_SINGLELINE | DT_VCENTER);

        RECT textRect = { rect.left + 8, rect.top + 24, rect.right - 8, rect.bottom - 6 };
        SetTextColor(hdc, RGB(232, 238, 244));
        DrawWrappedText(hdc, textRect, GetCommandSummary(command), DT_LEFT | DT_END_ELLIPSIS | DT_WORDBREAK);

        if (command.type == ScriptCommand::Type::Choice && sourceIndex == selectedCommandIndex_)
        {
            RECT badgeRect = { rect.right - 28, rect.top + 6, rect.right - 8, rect.top + 22 };
            HBRUSH badgeBrush = CreateSolidBrush(RGB(255, 215, 120));
            FillRect(hdc, &badgeRect, badgeBrush);
            DeleteObject(badgeBrush);
            SetTextColor(hdc, RGB(20, 24, 28));
            DrawTextW(hdc, std::to_wstring(selectedChoiceLinkIndex_ + 1).c_str(), -1, &badgeRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    for (size_t labelIndex : labelIndices)
    {
        const ScriptCommand& command = scenario_.commands[labelIndex];
        const std::wstring name = GetCommandParameter(command, L"name");
        RECT rect = labelRects[name];
        COLORREF fill = labelIndex == selectedCommandIndex_ ? RGB(96, 92, 52) : RGB(58, 54, 44);
        HBRUSH brush = CreateSolidBrush(fill);
        FillRect(hdc, &rect, brush);
        DeleteObject(brush);
        FrameRect(hdc, &rect, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));

        RECT textRect = { rect.left + 8, rect.top + 8, rect.right - 8, rect.bottom - 8 };
        SetTextColor(hdc, RGB(245, 238, 216));
        DrawWrappedText(hdc, textRect, L"*" + name, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }

    SelectObject(hdc, oldFont);
    DeleteObject(headerFont);
    DeleteObject(nodeFont);
}

std::wstring NovelRuntime::GetCommandTypeLabel(const ScriptCommand& command) const
{
    switch (command.type)
    {
    case ScriptCommand::Type::Title: return L"\u30bf\u30a4\u30c8\u30eb";
    case ScriptCommand::Type::Background: return L"\u80cc\u666f";
    case ScriptCommand::Type::Character: return L"\u7acb\u3061\u7d75";
    case ScriptCommand::Type::HideCharacter: return L"\u7acb\u3061\u7d75\u975e\u8868\u793a";
    case ScriptCommand::Type::Speaker: return L"\u8a71\u8005";
    case ScriptCommand::Type::ClearSpeaker: return L"\u8a71\u8005\u30af\u30ea\u30a2";
    case ScriptCommand::Type::Text: return L"\u672c\u6587";
    case ScriptCommand::Type::Choice: return L"\u9078\u629e\u80a2";
    case ScriptCommand::Type::SetValue: return L"\u5909\u6570\u8a2d\u5b9a";
    case ScriptCommand::Type::AddValue: return L"\u52a0\u7b97";
    case ScriptCommand::Type::IfJump: return L"\u6761\u4ef6\u5206\u5c90";
    case ScriptCommand::Type::Jump: return L"\u30b8\u30e3\u30f3\u30d7";
    case ScriptCommand::Type::Label: return L"\u30e9\u30d9\u30eb";
    }
    return L"\u4e0d\u660e";
}

std::wstring NovelRuntime::GetCommandSummary(const ScriptCommand& command) const
{
    if (command.type == ScriptCommand::Type::Text)
    {
        return GetCommandParameter(command, L"value");
    }
    if (command.type == ScriptCommand::Type::Speaker || command.type == ScriptCommand::Type::Title)
    {
        return GetCommandParameter(command, L"name");
    }
    if (command.type == ScriptCommand::Type::Background)
    {
        const std::wstring color = GetCommandParameter(command, L"color");
        return !color.empty() ? color : GetCommandParameter(command, L"storage");
    }
    if (command.type == ScriptCommand::Type::Character)
    {
        return GetCommandParameter(command, L"pos") + L" : " + GetCommandParameter(command, L"name");
    }
    if (command.type == ScriptCommand::Type::HideCharacter || command.type == ScriptCommand::Type::Jump || command.type == ScriptCommand::Type::Label)
    {
        const std::wstring pos = GetCommandParameter(command, L"pos");
        const std::wstring target = GetCommandParameter(command, L"target");
        const std::wstring name = GetCommandParameter(command, L"name");
        if (!pos.empty()) return pos;
        if (!target.empty()) return target;
        return name;
    }
    if (command.type == ScriptCommand::Type::SetValue || command.type == ScriptCommand::Type::AddValue)
    {
        return GetCommandParameter(command, L"name") + L" = " + GetCommandParameter(command, L"value");
    }
    if (command.type == ScriptCommand::Type::IfJump)
    {
        return GetCommandParameter(command, L"name") + L" " + GetCommandParameter(command, L"op") + L" " + GetCommandParameter(command, L"value");
    }
    if (command.type == ScriptCommand::Type::Choice)
    {
        return GetCommandParameter(command, L"prompt");
    }
    return L"";
}

void NovelRuntime::DrawCommandList(HDC hdc, const RECT& panelRect)
{
    SetBkMode(hdc, TRANSPARENT);
    HFONT headerFont = CreateFontW(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
    HFONT rowFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
    HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, headerFont));

    SetTextColor(hdc, RGB(240, 242, 246));
    RECT titleRect = { panelRect.left + 20, panelRect.top + 18, panelRect.right - 20, panelRect.top + 52 };
    DrawWrappedText(hdc, titleRect, L"Scenario Outline", DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    SelectObject(hdc, rowFont);
    const int rowHeight = 42;
    const int headerBottom = panelRect.top + 68;
    const size_t visibleRows = static_cast<size_t>((panelRect.bottom - headerBottom - 12) / rowHeight);
    const size_t total = scenario_.commands.size();
    const size_t lastIndex = total == 0 ? 0ull : total - 1;
    const size_t anchor = (std::min)(selectedCommandIndex_, lastIndex);
    size_t start = anchor > visibleRows / 2 ? anchor - visibleRows / 2 : 0;
    if (start + visibleRows > total)
    {
        start = total > visibleRows ? total - visibleRows : 0;
    }

    for (size_t i = 0; i < visibleRows && start + i < total; ++i)
    {
        const size_t commandIndex = start + i;
        const ScriptCommand& command = scenario_.commands[commandIndex];
        RECT rowRect = { panelRect.left + 12, headerBottom + static_cast<int>(i) * rowHeight, panelRect.right - 12, headerBottom + static_cast<int>(i + 1) * rowHeight - 6 };
        commandRowRects_.push_back(rowRect);
        commandRowIndices_.push_back(commandIndex);

        COLORREF rowColor = RGB(28, 35, 46);
        if (commandIndex == currentCommandIndex_ && !scenario_.commands.empty())
        {
            rowColor = RGB(47, 84, 122);
        }
        if (commandIndex == selectedCommandIndex_)
        {
            rowColor = RGB(83, 104, 66);
        }
        HBRUSH rowBrush = CreateSolidBrush(rowColor);
        FillRect(hdc, &rowRect, rowBrush);
        DeleteObject(rowBrush);

        RECT typeRect = { rowRect.left + 10, rowRect.top + 4, rowRect.left + 110, rowRect.bottom - 4 };
        SetTextColor(hdc, RGB(255, 225, 160));
        DrawWrappedText(hdc, typeRect, GetCommandTypeLabel(command), DT_LEFT | DT_SINGLELINE | DT_VCENTER);

        RECT summaryRect = { rowRect.left + 110, rowRect.top + 4, rowRect.right - 10, rowRect.bottom - 4 };
        SetTextColor(hdc, RGB(230, 236, 240));
        DrawWrappedText(hdc, summaryRect, GetCommandSummary(command), DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE | DT_VCENTER);
    }

    SelectObject(hdc, oldFont);
    DeleteObject(headerFont);
    DeleteObject(rowFont);
}

void NovelRuntime::DrawEventList(HDC hdc, const RECT& panelRect)
{
    currentEventListRect_ = panelRect;
    SetBkMode(hdc, TRANSPARENT);
    HFONT headerFont = CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
    HFONT bodyFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
    HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, headerFont));

    HBRUSH listBrush = CreateSolidBrush(RGB(18, 24, 32));
    FillRect(hdc, &panelRect, listBrush);
    DeleteObject(listBrush);
    FrameRect(hdc, &panelRect, static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));

    RECT titleRect = { panelRect.left + 12, panelRect.top + 8, panelRect.right - 12, panelRect.top + 34 };
    SetTextColor(hdc, RGB(228, 234, 240));
    DrawWrappedText(hdc, titleRect, L"\u30a4\u30d9\u30f3\u30c8\u4e00\u89a7", DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    eventAddTextRect_ = { panelRect.right - 244, panelRect.top + 8, panelRect.right - 184, panelRect.top + 32 };
    eventAddChoiceRect_ = { panelRect.right - 180, panelRect.top + 8, panelRect.right - 120, panelRect.top + 32 };
    eventDuplicateRect_ = { panelRect.right - 116, panelRect.top + 8, panelRect.right - 56, panelRect.top + 32 };
    eventDeleteRect_ = { panelRect.right - 52, panelRect.top + 8, panelRect.right - 12, panelRect.top + 32 };

    const struct
    {
        RECT rect;
        const wchar_t* label;
        COLORREF fill;
    } actions[] =
    {
        { eventAddTextRect_, L"+\u672c\u6587", RGB(54, 92, 134) },
        { eventAddChoiceRect_, L"+\u9078\u629e", RGB(102, 80, 140) },
        { eventDuplicateRect_, L"\u8907\u88fd", RGB(76, 96, 76) },
        { eventDeleteRect_, L"-", RGB(132, 58, 66) },
    };

    for (const auto& action : actions)
    {
        HBRUSH actionBrush = CreateSolidBrush(action.fill);
        FillRect(hdc, &action.rect, actionBrush);
        DeleteObject(actionBrush);
        FrameRect(hdc, &action.rect, static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));
        SetTextColor(hdc, RGB(245, 248, 250));
        DrawWrappedText(hdc, action.rect, action.label, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    }

    SelectObject(hdc, bodyFont);
    const int rowHeight = 28;
    const int startY = panelRect.top + 44;
    const size_t visibleRows = static_cast<size_t>((panelRect.bottom - startY - 8) / rowHeight);
    const size_t total = scenario_.commands.size();
    const size_t maxOffset = total > visibleRows ? total - visibleRows : 0;
    eventListScrollOffset_ = (std::max)(0, (std::min)(eventListScrollOffset_, static_cast<int>(maxOffset)));
    const size_t start = static_cast<size_t>(eventListScrollOffset_);

    for (size_t row = 0; row < visibleRows && start + row < total; ++row)
    {
        const size_t commandIndex = start + row;
        const ScriptCommand& command = scenario_.commands[commandIndex];
        RECT rowRect = { panelRect.left + 10, startY + static_cast<int>(row) * rowHeight, panelRect.right - 10, startY + static_cast<int>(row + 1) * rowHeight - 4 };
        eventRowRects_.push_back(rowRect);
        eventRowIndices_.push_back(commandIndex);

        HBRUSH rowBrush = CreateSolidBrush(commandIndex == selectedCommandIndex_ ? RGB(34, 44, 58) : RGB(22, 28, 38));
        FillRect(hdc, &rowRect, rowBrush);
        DeleteObject(rowBrush);

        RECT accentRect = { rowRect.left, rowRect.top, rowRect.left + 118, rowRect.bottom };
        HBRUSH accentBrush = CreateSolidBrush(GetCommandAccentColor(command));
        FillRect(hdc, &accentRect, accentBrush);
        DeleteObject(accentBrush);

        RECT typeRect = { accentRect.left + 8, accentRect.top + 2, accentRect.right - 8, accentRect.bottom - 2 };
        SetTextColor(hdc, RGB(255, 255, 255));
        DrawWrappedText(hdc, typeRect, GetCommandTypeLabel(command), DT_LEFT | DT_SINGLELINE | DT_VCENTER);

        RECT summaryRect = { accentRect.right + 10, rowRect.top + 2, rowRect.right - 30, rowRect.bottom - 2 };
        SetTextColor(hdc, RGB(206, 214, 222));
        DrawWrappedText(hdc, summaryRect, GetCommandSummary(command), DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE | DT_VCENTER);
    }

    SelectObject(hdc, oldFont);
    DeleteObject(headerFont);
    DeleteObject(bodyFont);
}

void NovelRuntime::DrawInspector(HDC hdc, const RECT& panelRect)
{
    inspectorEditTargets_.clear();
    inspectorCommitRect_ = {};
    inspectorCancelRect_ = {};
    SetBkMode(hdc, TRANSPARENT);
    HFONT headerFont = CreateFontW(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
    HFONT bodyFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
    HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, headerFont));

    HBRUSH panelBrush = CreateSolidBrush(RGB(18, 24, 32));
    FillRect(hdc, &panelRect, panelBrush);
    DeleteObject(panelBrush);

    SetTextColor(hdc, RGB(240, 242, 246));
    RECT titleRect = { panelRect.left + 20, panelRect.top + 18, panelRect.right - 20, panelRect.top + 52 };
    DrawWrappedText(hdc, titleRect, L"\u30a4\u30f3\u30b9\u30da\u30af\u30bf", DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    SelectObject(hdc, bodyFont);
    int cursorY = panelRect.top + 72;
    const int lineHeight = 26;
    auto drawLine = [&](const std::wstring& line, COLORREF color)
    {
        RECT lineRect = { panelRect.left + 20, cursorY, panelRect.right - 20, cursorY + lineHeight };
        SetTextColor(hdc, color);
        DrawWrappedText(hdc, lineRect, line, DT_LEFT | DT_WORDBREAK);
        cursorY += lineHeight;
    };
    auto drawEditable = [&](size_t commandIndex, const std::wstring& label, const std::wstring& key, const std::wstring& value)
    {
        RECT textRect = { panelRect.left + 20, cursorY, panelRect.right - 90, cursorY + lineHeight };
        RECT buttonRect = { panelRect.right - 80, cursorY, panelRect.right - 20, cursorY + lineHeight - 4 };
        SetTextColor(hdc, RGB(205, 214, 222));
        DrawWrappedText(hdc, textRect, label + L": " + value, DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE | DT_VCENTER);
        HBRUSH editBrush = CreateSolidBrush(RGB(58, 88, 118));
        FillRect(hdc, &buttonRect, editBrush);
        DeleteObject(editBrush);
        FrameRect(hdc, &buttonRect, static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));
        SetTextColor(hdc, RGB(245, 248, 250));
        DrawWrappedText(hdc, buttonRect, L"\u7de8\u96c6", DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        inspectorEditTargets_.push_back(InspectorEditTarget{ commandIndex, key, label, buttonRect });
        cursorY += lineHeight;
    };

    if (!scenario_.commands.empty() && selectedCommandIndex_ < scenario_.commands.size())
    {
        const ScriptCommand& command = scenario_.commands[selectedCommandIndex_];
        drawLine(L"\u7a2e\u985e: " + GetCommandTypeLabel(command), RGB(255, 225, 160));
        drawLine(L"\u884c\u756a\u53f7: " + std::to_wstring(command.sourceLine), RGB(205, 214, 222));
        drawLine(L"\u6982\u8981: " + GetCommandSummary(command), RGB(230, 236, 240));
        cursorY += 8;
        drawLine(L"\u30d1\u30e9\u30e1\u30fc\u30bf", RGB(255, 225, 160));
        if (command.parameters.empty())
        {
            drawLine(L"(\u306a\u3057)", RGB(180, 188, 196));
        }
        else
        {
            for (const auto& parameter : command.parameters)
            {
                drawEditable(selectedCommandIndex_, parameter.first, parameter.first, parameter.second);
            }
        }
        if (!command.links.empty())
        {
            cursorY += 8;
            drawLine(L"\u30ea\u30f3\u30af", RGB(255, 225, 160));
            for (size_t linkIndex = 0; linkIndex < command.links.size(); ++linkIndex)
            {
                const auto& link = command.links[linkIndex];
                const std::wstring prefix = linkIndex == selectedChoiceLinkIndex_ ? L"> " : L"  ";
                drawLine(prefix + std::to_wstring(linkIndex + 1) + L". " + link.first + L" -> " + link.second, linkIndex == selectedChoiceLinkIndex_ ? RGB(255, 225, 160) : RGB(205, 214, 222));
            }
            drawLine(L"1-9 \u3067\u679d\u3092\u9078\u3093\u3067\u304b\u3089\u30e9\u30d9\u30eb\u3092\u30af\u30ea\u30c3\u30af\u3059\u308b\u3068\u63a5\u7d9a\u5148\u3092\u5909\u66f4\u3067\u304d\u307e\u3059\u3002", RGB(180, 188, 196));
        }
        else if (command.type == ScriptCommand::Type::Jump || command.type == ScriptCommand::Type::IfJump)
        {
            cursorY += 8;
            drawLine(L"\u30d5\u30ed\u30fc\u30b0\u30e9\u30d5\u4e0a\u306e\u30e9\u30d9\u30eb\u3092\u30af\u30ea\u30c3\u30af\u3059\u308b\u3068\u63a5\u7d9a\u5148\u3092\u5909\u66f4\u3067\u304d\u307e\u3059\u3002", RGB(180, 188, 196));
        }
    }

    if (inspectorEditing_)
    {
        cursorY += 12;
        drawLine(L"\u7de8\u96c6\u4e2d", RGB(255, 225, 160));

        RECT inputRect = { panelRect.left + 20, cursorY, panelRect.right - 20, cursorY + 58 };
        HBRUSH inputBrush = CreateSolidBrush(RGB(22, 32, 44));
        FillRect(hdc, &inputRect, inputBrush);
        DeleteObject(inputBrush);
        FrameRect(hdc, &inputRect, static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));
        SetTextColor(hdc, RGB(236, 242, 246));
        RECT inputTextRect = { inputRect.left + 10, inputRect.top + 8, inputRect.right - 10, inputRect.bottom - 8 };
        DrawWrappedText(hdc, inputTextRect, editingLabel_ + L": " + editingBuffer_, DT_LEFT | DT_WORDBREAK);
        cursorY += 66;

        inspectorCommitRect_ = { panelRect.left + 20, cursorY, panelRect.left + 100, cursorY + 28 };
        inspectorCancelRect_ = { panelRect.left + 108, cursorY, panelRect.left + 188, cursorY + 28 };
        HBRUSH commitBrush = CreateSolidBrush(RGB(64, 112, 72));
        FillRect(hdc, &inspectorCommitRect_, commitBrush);
        DeleteObject(commitBrush);
        HBRUSH cancelBrush = CreateSolidBrush(RGB(122, 64, 70));
        FillRect(hdc, &inspectorCancelRect_, cancelBrush);
        DeleteObject(cancelBrush);
        FrameRect(hdc, &inspectorCommitRect_, static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));
        FrameRect(hdc, &inspectorCancelRect_, static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));
        DrawWrappedText(hdc, inspectorCommitRect_, L"\u4fdd\u5b58", DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        DrawWrappedText(hdc, inspectorCancelRect_, L"\u30ad\u30e3\u30f3\u30bb\u30eb", DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        cursorY += 36;
        drawLine(L"Enter \u3067\u4fdd\u5b58 / Esc \u3067\u30ad\u30e3\u30f3\u30bb\u30eb", RGB(180, 188, 196));
    }

    cursorY += 12;
    drawLine(L"\u5909\u6570", RGB(255, 225, 160));
    if (variables_.empty())
    {
        drawLine(L"(\u7a7a)", RGB(180, 188, 196));
    }
    else
    {
        for (const auto& variable : variables_)
        {
            drawLine(variable.first + L" = " + variable.second, RGB(205, 214, 222));
        }
    }

    SelectObject(hdc, oldFont);
    DeleteObject(headerFont);
    DeleteObject(bodyFont);
}

bool NovelRuntime::TrySelectCommandFromPoint(POINT point, const RECT& clientRect)
{
    UNREFERENCED_PARAMETER(clientRect);
    for (size_t i = 0; i < commandRowRects_.size(); ++i)
    {
        if (PtInRect(&commandRowRects_[i], point))
        {
            selectedCommandIndex_ = commandRowIndices_[i];
            return true;
        }
    }

    for (size_t i = 0; i < graphNodeRects_.size(); ++i)
    {
        if (PtInRect(&graphNodeRects_[i], point))
        {
            return HandleGraphNodeSelection(graphNodeIndices_[i]);
        }
    }

    for (size_t i = 0; i < eventRowRects_.size(); ++i)
    {
        if (PtInRect(&eventRowRects_[i], point))
        {
            selectedCommandIndex_ = eventRowIndices_[i];
            return true;
        }
    }

    return false;
}
