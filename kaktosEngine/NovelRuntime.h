#pragma once

#include "Scenario.h"

#include <objidl.h>
#include <gdiplus.h>
#include <memory>

struct SceneListItem
{
    std::wstring path;
    std::wstring label;
    RECT rect = {};
};

struct AssetListItem
{
    std::wstring category;
    std::wstring path;
    std::wstring label;
    RECT rect = {};
};

struct CharacterSlot
{
    std::wstring slotName;
    std::wstring displayName;
    std::wstring imagePath;
    std::unique_ptr<Gdiplus::Image> image;
};

struct ToolbarItem
{
    std::wstring id;
    std::wstring label;
    std::wstring iconPath;
    std::unique_ptr<Gdiplus::Image> iconImage;
};

struct InspectorEditTarget
{
    size_t commandIndex = 0;
    std::wstring key;
    std::wstring label;
    RECT buttonRect = {};
};

class NovelRuntime
{
public:
    enum class DragHandle
    {
        None,
        LeftPanel,
        RightPanel,
        GraphHeight,
        EventListHeight,
    };

    bool Initialize();
    void Shutdown();

    void LoadScenario(const std::wstring& requestedPath = L"");
    void Advance();
    bool HandleClick(POINT point);
    bool HandleMouseDown(POINT point);
    bool HandleMouseMove(POINT point);
    bool HandleMouseUp(POINT point);
    bool HandleMouseWheel(short delta, POINT point);
    bool HandleKeyDown(WPARAM key);
    bool HandleChar(wchar_t ch);
    bool HandleViewMenuCommand(UINT commandId);
    bool IsViewMenuChecked(UINT commandId) const;
    void ResetLayout();
    void Draw(HDC hdc, const RECT& clientRect);

    const std::wstring& GetWindowTitle() const;

private:
    bool JumpToLabel(const std::wstring& target);
    void ActivateChoice(const ScriptCommand& command);
    void SelectChoice(size_t index);
    void ApplyBackgroundCommand(const ScriptCommand& command);
    void ApplyCharacterCommand(const ScriptCommand& command);
    void ApplyHideCharacterCommand(const ScriptCommand& command);
    void ApplySetValueCommand(const ScriptCommand& command);
    void ApplyAddValueCommand(const ScriptCommand& command);
    void ApplyIfJumpCommand(const ScriptCommand& command);
    bool EvaluateCondition(const ScriptCommand& command) const;
    CharacterSlot* GetCharacterSlot(const std::wstring& position);
    std::wstring GetCommandParameter(const ScriptCommand& command, const std::wstring& key) const;
    bool TryParseHexColor(const std::wstring& value, COLORREF& color) const;
    bool TryGetNumber(const std::wstring& value, long long& number) const;
    std::unique_ptr<Gdiplus::Image> TryLoadImage(const std::wstring& fullPath) const;
    void DrawWrappedText(HDC hdc, const RECT& bounds, const std::wstring& text, UINT format) const;
    void DrawCharacterSlot(HDC hdc, const RECT& stageRect, CharacterSlot& slot, int centerX) const;
    void DrawChoices(HDC hdc, const RECT& messageRect);
    void DrawToolbar(HDC hdc, const RECT& previewRect);
    void DrawCommandPalette(HDC hdc, const RECT& panelRect);
    void DrawCommandList(HDC hdc, const RECT& panelRect);
    void DrawInspector(HDC hdc, const RECT& panelRect);
    void DrawNodeGraph(HDC hdc, const RECT& panelRect);
    void DrawEventList(HDC hdc, const RECT& panelRect);
    COLORREF GetCommandAccentColor(const ScriptCommand& command) const;
    bool HandleGraphNodeSelection(size_t commandIndex);
    bool HandleToolbarClick(POINT point);
    void InitializeToolbarItems();
    void LoadToolbarIcons();
    bool IsEditableSourceNode(size_t commandIndex) const;
    bool IsLabelNode(size_t commandIndex) const;
    void RewireSelectedSourceToLabel(size_t labelCommandIndex);
    std::wstring GetCommandTypeLabel(const ScriptCommand& command) const;
    std::wstring GetCommandSummary(const ScriptCommand& command) const;
    bool TrySelectCommandFromPoint(POINT point, const RECT& clientRect);
    DragHandle HitTestDragHandle(POINT point) const;
    void DrawSplitter(HDC hdc, const RECT& rect, bool active) const;
    RECT GetPreviewRect(const RECT& clientRect) const;
    RECT GetLeftPanelRect(const RECT& clientRect) const;
    RECT GetRightPanelRect(const RECT& clientRect) const;
    RECT GetGraphRect(const RECT& previewRect) const;
    RECT GetStageRect(const RECT& previewRect) const;
    RECT GetMessageRect(const RECT& previewRect) const;
    RECT GetEventListRect(const RECT& previewRect) const;
    RECT GetToolbarRect(const RECT& previewRect) const;
    int GetPreviewContentTop(const RECT& previewRect) const;
    bool HasVisibleArea(const RECT& rect) const;
    ScriptCommand* GetSelectedCommand();
    const ScriptCommand* GetSelectedCommand() const;
    void InsertCommandAfterSelection(ScriptCommand::Type type);
    void DeleteSelectedCommand();
    void DuplicateSelectedCommand();
    bool SaveProject();
    bool SaveProjectAs();
    void LoadProjectSettings(const std::wstring& projectPath);
    void RefreshSceneList();
    void RefreshAssetList();
    bool HandleSceneClick(POINT point);
    bool HandleAssetClick(POINT point);
    bool CreateNewScene();
    bool RenameCurrentScene();
    void BeginInspectorEdit(size_t commandIndex, const std::wstring& key, const std::wstring& label, const std::wstring& initialValue);
    void CommitInspectorEdit();
    void CancelInspectorEdit();
    bool HandleInspectorClick(POINT point);
    bool HandleEventActionClick(POINT point);
    void SyncDocumentMetadata();

private:
    ULONG_PTR gdiplusToken_ = 0;
    std::wstring storyTitle_ = L"Kaktos Engine";
    std::wstring speakerName_;
    std::wstring currentText_ = L"Loading scenario...";
    std::wstring statusText_;
    std::wstring scenarioBaseDir_;
    std::wstring scenarioPath_;
    std::wstring projectPath_;
    std::wstring backgroundPath_;
    std::unique_ptr<Gdiplus::Image> backgroundImage_;
    COLORREF backgroundColor_ = RGB(28, 36, 48);
    CharacterSlot leftCharacter_ = { L"left" };
    CharacterSlot centerCharacter_ = { L"center" };
    CharacterSlot rightCharacter_ = { L"right" };
    std::vector<ToolbarItem> toolbarItems_;
    std::vector<SceneListItem> sceneItems_;
    std::vector<AssetListItem> assetItems_;
    ScenarioDocument scenario_;
    std::unordered_map<std::wstring, std::wstring> variables_;
    size_t currentCommandIndex_ = 0;
    std::vector<std::pair<std::wstring, std::wstring>> activeChoices_;
    std::vector<RECT> activeChoiceRects_;
    std::vector<RECT> commandRowRects_;
    std::vector<size_t> commandRowIndices_;
    std::vector<RECT> graphNodeRects_;
    std::vector<size_t> graphNodeIndices_;
    std::vector<RECT> eventRowRects_;
    std::vector<size_t> eventRowIndices_;
    std::vector<RECT> toolbarButtonRects_;
    std::vector<InspectorEditTarget> inspectorEditTargets_;
    RECT eventAddTextRect_ = {};
    RECT eventAddChoiceRect_ = {};
    RECT eventDeleteRect_ = {};
    RECT eventDuplicateRect_ = {};
    RECT sceneAddRect_ = {};
    RECT sceneRenameRect_ = {};
    RECT inspectorCommitRect_ = {};
    RECT inspectorCancelRect_ = {};
    RECT leftSplitterRect_ = {};
    RECT rightSplitterRect_ = {};
    RECT graphSplitterRect_ = {};
    RECT eventSplitterRect_ = {};
    RECT currentEventListRect_ = {};
    RECT lastClientRect_ = { 0, 0, 1280, 720 };
    size_t selectedCommandIndex_ = 0;
    size_t selectedChoiceLinkIndex_ = 0;
    int leftPanelWidth_ = 280;
    int rightPanelWidth_ = 320;
    int graphHeight_ = 162;
    int eventListHeight_ = 208;
    int eventListScrollOffset_ = 0;
    DragHandle activeDragHandle_ = DragHandle::None;
    size_t editingCommandIndex_ = 0;
    std::wstring editingKey_;
    std::wstring editingLabel_;
    std::wstring editingBuffer_;
    bool showComponents_ = true;
    bool showInspector_ = true;
    bool showFlowGraph_ = false;
    bool showPreviewPanel_ = false;
    bool showEventList_ = false;
    bool inspectorEditing_ = false;
    bool previewVisible_ = false;
    bool waitingForChoice_ = false;
    bool reachedEnd_ = false;
};
