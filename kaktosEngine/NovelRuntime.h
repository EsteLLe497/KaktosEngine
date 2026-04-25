#pragma once

#include "Scenario.h"

#include <objidl.h>
#include <wrl/client.h>
#include <xaudio2.h>
#include <gdiplus.h>
#include <memory>
#include <windows.h>

struct EditorSnapshot
{
    std::wstring scenarioText;
    size_t selectedCommandIndex = 0;
};

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
    std::wstring relativePath;
    int depth = 0;
    bool isDirectory = false;
    RECT rect = {};
};

struct CharacterSlot
{
    std::wstring slotName;
    std::wstring displayName;
    std::wstring imagePath;
    std::unique_ptr<Gdiplus::Image> image;
    bool visible = true;
    int offsetX = 0;
    int offsetY = 0;
    int scale = 100;
    int opacity = 255;
};

struct ToolbarItem
{
    std::wstring id;
    std::wstring label;
    std::wstring iconPath;
    std::unique_ptr<Gdiplus::Image> iconImage;
};

struct PaletteButtonItem
{
    std::wstring label;
    ScriptCommand::Type type = ScriptCommand::Type::Text;
    RECT rect = {};
};

struct PaletteSectionItem
{
    std::wstring title;
    COLORREF color = RGB(0, 0, 0);
    RECT rect = {};
    RECT toggleRect = {};
    bool expanded = true;
};

struct LeftTabItem
{
    std::wstring id;
    std::wstring label;
    RECT rect = {};
};

struct AssetCategoryItem
{
    std::wstring id;
    std::wstring label;
    std::wstring iconPath;
    RECT rect = {};
};

struct FontAssetItem
{
    std::wstring path;
    std::wstring label;
    std::wstring family;
};

struct InspectorEditTarget
{
    size_t commandIndex = 0;
    std::wstring key;
    std::wstring label;
    RECT buttonRect = {};
};

struct InspectorActionTarget
{
    std::wstring action;
    size_t commandIndex = 0;
    size_t linkIndex = 0;
    RECT buttonRect = {};
};

struct CharacterExpressionDefinition
{
    std::wstring name;
    std::wstring imagePath;
};

struct CharacterDefinition
{
    std::wstring id;
    std::wstring displayName;
    std::wstring baseImagePath;
    std::wstring color = L"#ffffff";
    std::vector<CharacterExpressionDefinition> expressions;
};

struct CharacterManagerActionTarget
{
    std::wstring action;
    size_t characterIndex = static_cast<size_t>(-1);
    size_t expressionIndex = static_cast<size_t>(-1);
    RECT buttonRect = {};
};

struct EditorSettings
{
    int windowWidth = 1280;
    int windowHeight = 720;
    std::wstring defaultFont = L"Yu Gothic UI";
    int defaultTextSpeed = 40;
    int masterVolume = 100;
    int bgmVolume = 100;
    int seVolume = 100;
    int voiceVolume = 100;
    std::wstring saveDirectory;
    bool autosaveEnabled = true;
    bool defaultMessageWindowVisible = true;
    COLORREF defaultMessageWindowColor = RGB(8, 10, 14);
    COLORREF defaultMessageWindowBorderColor = RGB(122, 128, 138);
    int defaultMessageWindowOpacity = 178;
    int defaultMessageWindowPadding = 24;
    std::wstring defaultMessageWindowImage;
    bool defaultNameWindowVisible = true;
    COLORREF defaultNameWindowColor = RGB(12, 18, 28);
    COLORREF defaultNameWindowBorderColor = RGB(80, 132, 180);
    int defaultNameWindowOpacity = 214;
    int defaultNameWindowOffsetX = 0;
    int defaultNameWindowOffsetY = 0;
    int defaultNameWindowWidth = 220;
    int defaultNameWindowHeight = 36;
    int defaultNameWindowPadding = 12;
    std::wstring defaultNameWindowImage;
};

struct SettingsActionTarget
{
    std::wstring action;
    RECT buttonRect = {};
};

enum class VariableType
{
    Bool,
    Integer,
    String,
};

struct VariableDefinition
{
    std::wstring name;
    VariableType type = VariableType::String;
    std::wstring initialValue;
    std::wstring description;
};

struct VariableManagerActionTarget
{
    std::wstring action;
    size_t variableIndex = static_cast<size_t>(-1);
    RECT buttonRect = {};
};

struct UiButtonDefinition
{
    std::wstring id;
    std::wstring label;
    std::wstring iconPath;
    int x = 0;
    int y = 0;
    int width = 72;
    int height = 28;
    bool visible = true;
    std::unique_ptr<Gdiplus::Image> iconImage;
};

struct ActiveChoiceItem
{
    std::wstring text;
    std::wstring target;
    bool enabled = true;
};

enum class AudioChannel
{
    Bgm,
    Se,
    Voice,
    Preview,
};

struct AudioPlaybackState
{
    IXAudio2SourceVoice* voice = nullptr;
    std::vector<BYTE> formatBytes;
    std::vector<BYTE> audioBytes;
    bool looping = false;
    bool usesLegacyMci = false;
    std::wstring legacyAlias;
    std::wstring sourcePath;
};

struct ProjectLauncherRow
{
    std::wstring projectPath;
    RECT rowRect = {};
    RECT dataRect = {};
    RECT deleteRect = {};
};

class NovelRuntime
{
public:
    enum class LeftPanelTab
    {
        Components,
        Materials,
        Scenario,
    };

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
    bool HandleDoubleClick(POINT point);
    bool HandleMouseDown(POINT point);
    bool HandleMouseMove(POINT point);
    bool HandleMouseUp(POINT point);
    bool HandleMouseWheel(short delta, POINT point);
    bool HandleRightClick(POINT clientPoint, POINT screenPoint);
    bool HandleFileDrop(POINT clientPoint, const std::vector<std::wstring>& paths);
    bool HandleKeyDown(WPARAM key);
    bool HandleChar(wchar_t ch);
    bool HandleControlCommand(WPARAM wParam, LPARAM lParam);
    bool HandlePreviewClick(POINT point);
    bool HandlePreviewMouseDown(POINT point);
    bool HandlePreviewMouseMove(POINT point);
    bool HandlePreviewMouseUp(POINT point);
    bool HandleTimer();
    bool HandleViewMenuCommand(UINT commandId);
    bool IsViewMenuChecked(UINT commandId) const;
    void ResetLayout();
    void Draw(HDC hdc, const RECT& clientRect);
    void DrawPreviewWindow(HDC hdc, const RECT& clientRect);
    void SetPlayerMode(bool enabled);
    bool IsPlayerMode() const;
    void SetHostWindow(HWND hWnd);
    void NotifyPreviewWindowDestroyed();
    void RefreshPreviewWindow();
    void ShowProjectLauncher();

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
    void ApplyBgmCommand(const ScriptCommand& command);
    void ApplySeCommand(const ScriptCommand& command);
    void ApplyVoiceCommand(const ScriptCommand& command);
    void ApplyWaitCommand(const ScriptCommand& command);
    void ApplyClearTextCommand();
    void ApplyMessageWindowCommand(const ScriptCommand& command);
    void ApplyTextSpeedCommand(const ScriptCommand& command);
    void ApplyMessageFontCommand(const ScriptCommand& command);
    void ApplyMessageFontResetCommand();
    void ApplyMessageStyleCommand(const ScriptCommand& command);
    void ApplyTextColorCommand(const ScriptCommand& command);
    void ApplyNameColorCommand(const ScriptCommand& command);
    void ApplyNameWindowCommand(const ScriptCommand& command);
    void ApplyVerticalTextCommand(const ScriptCommand& command);
    void ApplyPageBreakCommand();
    void ApplyShakeCommand(const ScriptCommand& command);
    void ApplyFadeCommand(const ScriptCommand& command);
    void ApplyTransitionCommand(const ScriptCommand& command);
    void ApplyZoomCommand(const ScriptCommand& command);
    void ApplyPanCommand(const ScriptCommand& command);
    void ApplyFlashCommand(const ScriptCommand& command);
    void ApplyTintCommand(const ScriptCommand& command);
    bool InitializeAudioEngine();
    void ShutdownAudioEngine();
    bool DecodeAudioFile(const std::wstring& fullPath, std::vector<BYTE>& formatBytes, std::vector<BYTE>& audioBytes);
    bool PlayAudioFile(AudioChannel channel, const std::wstring& fullPath, bool loop, int volumePercent);
    void StopAudioChannel(AudioChannel channel);
    void SetAudioChannelVolume(AudioChannel channel, int volumePercent);
    AudioPlaybackState& GetAudioPlaybackState(AudioChannel channel);
    const AudioPlaybackState& GetAudioPlaybackState(AudioChannel channel) const;
    void StopBgmPlayback();
    void ApplyIfJumpCommand(const ScriptCommand& command);
    bool EvaluateCondition(const ScriptCommand& command) const;
    CharacterSlot* GetCharacterSlot(const std::wstring& position);
    std::wstring GetCommandParameter(const ScriptCommand& command, const std::wstring& key) const;
    bool TryParseHexColor(const std::wstring& value, COLORREF& color) const;
    std::wstring FormatHexColor(COLORREF color) const;
    COLORREF ShowColorPresetMenu(POINT point, COLORREF currentColor) const;
    bool TryGetNumber(const std::wstring& value, long long& number) const;
    std::unique_ptr<Gdiplus::Image> TryLoadImage(const std::wstring& fullPath) const;
    void DrawWrappedText(HDC hdc, const RECT& bounds, const std::wstring& text, UINT format) const;
    void DrawVerticalText(HDC hdc, const RECT& bounds, const std::wstring& text, int lineHeight) const;
    void DrawCharacterSlot(HDC hdc, const RECT& stageRect, CharacterSlot& slot, int centerX) const;
    void DrawChoices(HDC hdc, const RECT& messageRect);
    void PushBacklogEntry(const std::wstring& speaker, const std::wstring& text);
    void PushVariableHistory(const std::wstring& entry);
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
    void LoadUiButtonIcons();
    bool IsEditableSourceNode(size_t commandIndex) const;
    bool IsLabelNode(size_t commandIndex) const;
    void RewireSelectedSourceToLabel(size_t labelCommandIndex);
    std::wstring GetCommandTypeLabel(const ScriptCommand& command) const;
    std::wstring GetCommandSummary(const ScriptCommand& command) const;
    bool TrySelectCommandFromPoint(POINT point, const RECT& clientRect);
    size_t FindEventIndexFromPoint(POINT point) const;
    std::wstring FindScenePathFromPoint(POINT point) const;
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
    ScriptCommand CreateDefaultCommand(ScriptCommand::Type type) const;
    void InsertCommandAtIndex(ScriptCommand::Type type, size_t insertIndex);
public:
    bool ExecuteEditorCommand(UINT commandId);
private:
    bool CopySelectedCommand();
    bool CutSelectedCommand();
    bool PasteCopiedCommand();
    bool ApplyAssetToCommand(size_t commandIndex, const AssetListItem& item);
    bool ImportMaterialFiles(const std::vector<std::wstring>& paths, const std::wstring& category);
    void DeleteSelectedCommand();
    void DuplicateSelectedCommand();
    void MoveSelectedCommand(int delta);
    void ToggleSelectedCommandEnabled();
    bool SaveProject();
    bool SaveProjectAs();
    bool CreateProjectFromDialog();
    bool LoadProjectFromDialog();
    bool LoadProjectFile(const std::wstring& projectPath);
    std::wstring BuildDefaultProjectSettingsText(const std::wstring& scenarioPath) const;
    std::wstring GetRecentProjectsPath() const;
    void LoadRecentProjects();
    void SaveRecentProjects() const;
    void AddRecentProject(const std::wstring& projectPath);
    void RemoveRecentProject(const std::wstring& projectPath);
    std::wstring GetProjectRootFromProjectPath(const std::wstring& projectPath) const;
    bool OpenProjectFolder(const std::wstring& projectPath);
    bool DeleteProjectToRecycleBin(const std::wstring& projectPath);
    bool HandleProjectLauncherClick(POINT point);
    bool ExportBuild();
    bool SaveRuntimeStateAs();
    bool LoadRuntimeStateFromDialog();
    bool SaveRuntimeStateToPath(const std::wstring& savePath);
    bool LoadRuntimeStateFromPath(const std::wstring& savePath);
    void ApplyLoadedCharacterState(CharacterSlot& slot, const std::wstring& slotName, const std::wstring& displayName, const std::wstring& imagePath);
    void LoadProjectSettings(const std::wstring& projectPath);
    void ApplyEditorUiDefaults();
    std::wstring MakeProjectRelativeAssetPath(const std::wstring& fullPath) const;
    std::wstring FindDefaultTextboxImagePath() const;
    void RefreshSceneList();
    void RefreshAssetList();
    void ResetPlaybackState();
    void PrimePreviewState(size_t startIndex);
    void StartPreviewFromIndex(size_t startIndex);
    void StartPreviewFromSelection();
    void RefreshPreviewIfActive();
    void ToggleSkipMode();
    void ToggleAutoMode();
    void TogglePreviewFullscreen();
    void DrawSceneCreateDialog(HDC hdc, const RECT& clientRect);
    void DrawProjectLauncher(HDC hdc, const RECT& clientRect);
    void DrawProjectDialog(HDC hdc, const RECT& clientRect);
    void DrawCharacterManagerDialog(HDC hdc, const RECT& clientRect);
    void DrawVariableManagerDialog(HDC hdc, const RECT& clientRect);
    void DrawSettingsDialog(HDC hdc, const RECT& clientRect);
    void UpdateChildControls();
    void EnsureChildControls();
    void DestroyChildControls();
    bool HandleSceneClick(POINT point);
    bool HandleAssetClick(POINT point);
    bool AddMaterialFile();
    bool CreateNewScene();
    bool DuplicateCurrentScene();
    bool DeleteCurrentScene();
    void ShowCreateSceneDialog();
    void HideCreateSceneDialog();
    void ShowProjectDialog();
    void HideProjectDialog();
    void ShowCharacterManagerDialog();
    void HideCharacterManagerDialog();
    void ShowVariableManagerDialog();
    void HideVariableManagerDialog();
    void ShowSettingsDialog();
    void HideSettingsDialog();
    bool RenameCurrentScene();
    bool RestoreAutosaveSnapshot(bool notifyOnMissing);
    void SaveAutosaveSnapshot();
    void DeleteAutosaveSnapshot();
    std::wstring GetAutosavePath() const;
    std::wstring GetQuickSavePath() const;
    std::wstring SerializeProjectSettings() const;
    void BeginInspectorEdit(size_t commandIndex, const std::wstring& key, const std::wstring& label, const std::wstring& initialValue);
    void CommitInspectorEdit();
    void CancelInspectorEdit();
    bool HandleInspectorClick(POINT point);
    bool BrowseCommandAsset(size_t commandIndex, const std::wstring& key, bool audio);
    void RefreshAvailableFonts();
    std::wstring GetFontsDirectory() const;
    std::wstring GetNextAvailableFont(const std::wstring& current) const;
    bool HandleEventActionClick(POINT point);
    void BeginCharacterFieldEdit(const std::wstring& title, const std::wstring& action, size_t characterIndex, size_t expressionIndex, const std::wstring& initialValue);
    void CommitCharacterFieldEdit();
    void CancelCharacterFieldEdit();
    void BeginVariableFieldEdit(const std::wstring& title, const std::wstring& action, size_t variableIndex, const std::wstring& initialValue);
    void CommitVariableFieldEdit();
    void CancelVariableFieldEdit();
    const CharacterDefinition* FindCharacterDefinition(const std::wstring& id) const;
    CharacterDefinition* FindCharacterDefinition(const std::wstring& id);
    std::wstring GetCharacterDefinitionLabel(const CharacterDefinition& definition) const;
    std::wstring GetEffectTargetLabel(const std::wstring& target) const;
    void SyncVariableDefinitions();
    std::wstring ShowVariableSelectionMenu(POINT point, const std::wstring& currentName) const;
    std::wstring ShowFontSelectionMenu(POINT point, const std::wstring& currentName) const;
    size_t GetVariableUsageCount(const std::wstring& name) const;
    void SyncDocumentMetadata();
    void PushUndoSnapshot();
    bool RestoreSnapshot(const EditorSnapshot& snapshot);
    void Undo();
    void Redo();
    std::vector<size_t> BuildFilteredEventIndices() const;
    bool MatchesEventFilter(const ScriptCommand& command) const;
    void TogglePreviewWindow();
    void DrawPreviewSurface(HDC hdc, const RECT& clientRect, bool standalone);
    void NormalizePlaybackStateAfterScenarioMutation();
    std::wstring GetVariableTypeLabel(VariableType type) const;
    std::wstring GetAssetsRootDirectory() const;
    std::wstring GetScenarioDirectory() const;
    std::wstring ResolveMaterialIconPath(const std::wstring& baseRelativePath) const;
    void StopAssetPreviewAudio();
    void StartAssetPreview(const AssetListItem& item);
    std::wstring EscapeSaveValue(const std::wstring& value) const;
    std::wstring UnescapeSaveValue(const std::wstring& value) const;
    std::wstring BrowseForFolder(const std::wstring& title, const std::wstring& initialPath) const;

private:
    ULONG_PTR gdiplusToken_ = 0;
    bool comInitialized_ = false;
    bool mediaFoundationInitialized_ = false;
    Microsoft::WRL::ComPtr<IXAudio2> xaudio2_;
    IXAudio2MasteringVoice* masteringVoice_ = nullptr;
    AudioPlaybackState bgmPlayback_;
    AudioPlaybackState sePlayback_;
    AudioPlaybackState voicePlayback_;
    AudioPlaybackState previewPlayback_;
    HWND hostWindow_ = nullptr;
    HWND previewWindow_ = nullptr;
    HWND eventSearchEdit_ = nullptr;
    HWND inspectorEdit_ = nullptr;
    HWND eventTextEdit_ = nullptr;
    HWND sceneNameEdit_ = nullptr;
    HWND characterFieldEdit_ = nullptr;
    HWND variableFieldEdit_ = nullptr;
    std::wstring storyTitle_ = L"Kaktos Engine";
    std::wstring speakerName_;
    std::wstring currentText_ = L"Loading scenario...";
    std::wstring displayedText_;
    std::wstring statusText_;
    std::wstring scenarioBaseDir_;
    std::wstring scenarioPath_;
    std::wstring projectPath_;
    std::wstring currentBgmPath_;
    std::wstring lastAudioDebugMessage_;
    std::wstring messageFontFace_ = L"Yu Gothic UI";
    std::wstring messageWindowImagePath_;
    std::wstring nameWindowImagePath_;
    std::wstring backgroundPath_;
    std::wstring backgroundDisplayName_;
    std::unique_ptr<Gdiplus::Image> messageWindowImage_;
    std::unique_ptr<Gdiplus::Image> nameWindowImage_;
    std::unique_ptr<Gdiplus::Image> backgroundImage_;
    COLORREF backgroundColor_ = RGB(28, 36, 48);
    COLORREF messageWindowColor_ = RGB(8, 10, 14);
    COLORREF messageWindowBorderColor_ = RGB(122, 128, 138);
    COLORREF messageTextColor_ = RGB(242, 244, 247);
    COLORREF nameTextColor_ = RGB(123, 203, 255);
    COLORREF nameWindowColor_ = RGB(12, 18, 28);
    COLORREF nameWindowBorderColor_ = RGB(80, 132, 180);
    bool backgroundVisible_ = true;
    bool messageWindowVisible_ = true;
    bool nameBoxVisible_ = true;
    bool verticalTextEnabled_ = false;
    bool textRevealActive_ = false;
    int textSpeedMs_ = 40;
    int messageWindowOpacity_ = 178;
    int messageWindowPadding_ = 24;
    int nameWindowOpacity_ = 214;
    int nameWindowOffsetX_ = 0;
    int nameWindowOffsetY_ = 0;
    int nameWindowWidth_ = 220;
    int nameWindowHeight_ = 36;
    int nameWindowPadding_ = 12;
    size_t textRevealIndex_ = 0;
    DWORD nextTextRevealTick_ = 0;
    int backgroundOffsetX_ = 0;
    int backgroundOffsetY_ = 0;
    int backgroundScale_ = 100;
    int backgroundOpacity_ = 255;
    CharacterSlot leftCharacter_ = { L"left" };
    CharacterSlot centerCharacter_ = { L"center" };
    CharacterSlot rightCharacter_ = { L"right" };
    std::vector<ToolbarItem> toolbarItems_;
    std::vector<PaletteButtonItem> paletteButtons_;
    std::vector<PaletteSectionItem> paletteSections_;
    std::vector<LeftTabItem> leftTabs_;
    std::vector<AssetCategoryItem> assetCategories_;
    std::vector<SceneListItem> sceneItems_;
    std::vector<AssetListItem> assetItems_;
    std::vector<FontAssetItem> availableFonts_;
    std::vector<std::wstring> loadedPrivateFontPaths_;
    std::vector<VariableDefinition> variableDefinitions_;
    std::vector<std::wstring> recentProjects_;
    std::vector<RECT> variableDefinitionRects_;
    std::vector<VariableManagerActionTarget> variableManagerActionTargets_;
    ScenarioDocument scenario_;
    std::unordered_map<std::wstring, std::wstring> variables_;
    std::vector<std::wstring> variableHistory_;
    size_t currentCommandIndex_ = 0;
    std::vector<ActiveChoiceItem> activeChoices_;
    std::vector<RECT> activeChoiceRects_;
    std::vector<RECT> commandRowRects_;
    std::vector<size_t> commandRowIndices_;
    std::vector<RECT> graphNodeRects_;
    std::vector<size_t> graphNodeIndices_;
    std::vector<RECT> eventRowRects_;
    std::vector<size_t> eventRowIndices_;
    std::vector<RECT> eventExpandRects_;
    std::vector<size_t> eventExpandIndices_;
    std::vector<RECT> toolbarButtonRects_;
    std::vector<ProjectLauncherRow> projectLauncherRows_;
    std::vector<InspectorEditTarget> inspectorEditTargets_;
    std::vector<InspectorActionTarget> inspectorActionTargets_;
    RECT eventAddTextRect_ = {};
    RECT eventAddChoiceRect_ = {};
    RECT eventDeleteRect_ = {};
    RECT eventDuplicateRect_ = {};
    RECT sceneAddRect_ = {};
    RECT sceneRenameRect_ = {};
    RECT sceneDuplicateRect_ = {};
    RECT sceneDeleteRect_ = {};
    RECT sceneDialogRect_ = {};
    RECT sceneDialogCreateRect_ = {};
    RECT sceneDialogCancelRect_ = {};
    RECT sceneDialogEditRect_ = {};
    RECT projectDialogRect_ = {};
    RECT projectDialogCreateRect_ = {};
    RECT projectDialogOpenRect_ = {};
    RECT projectDialogCancelRect_ = {};
    RECT projectDialogEditRect_ = {};
    RECT projectLauncherPanelRect_ = {};
    RECT projectLauncherCreateRect_ = {};
    RECT projectLauncherOpenRect_ = {};
    RECT characterDialogRect_ = {};
    RECT characterDialogAddRect_ = {};
    RECT characterDialogDeleteRect_ = {};
    RECT characterDialogCloseRect_ = {};
    RECT characterDialogEditRect_ = {};
    RECT characterDialogFieldDialogRect_ = {};
    RECT characterDialogFieldEditRect_ = {};
    RECT characterDialogFieldOkRect_ = {};
    RECT characterDialogFieldCancelRect_ = {};
    RECT variableDialogRect_ = {};
    RECT variableDialogAddRect_ = {};
    RECT variableDialogDeleteRect_ = {};
    RECT variableDialogCloseRect_ = {};
    RECT variableDialogEditRect_ = {};
    RECT variableFieldDialogRect_ = {};
    RECT variableFieldEditRect_ = {};
    RECT variableFieldOkRect_ = {};
    RECT variableFieldCancelRect_ = {};
    RECT settingsDialogRect_ = {};
    RECT settingsNavRect_ = {};
    RECT settingsContentRect_ = {};
    RECT settingsDialogCloseRect_ = {};
    RECT settingsDialogRestoreRect_ = {};
    RECT materialAddRect_ = {};
    RECT materialPreviewRect_ = {};
    RECT materialPreviewPlayRect_ = {};
    RECT materialPreviewStopRect_ = {};
    RECT materialPreviewVolumeDownRect_ = {};
    RECT materialPreviewVolumeUpRect_ = {};
    RECT previewMenuButtonRect_ = {};
    RECT previewMenuPanelRect_ = {};
    RECT previewMenuSaveRect_ = {};
    RECT previewMenuLoadRect_ = {};
    RECT previewMenuLogRect_ = {};
    RECT previewMenuSkipRect_ = {};
    RECT previewMenuAutoRect_ = {};
    RECT previewMenuConfigRect_ = {};
    RECT previewMenuFullscreenRect_ = {};
    RECT previewMenuCloseRect_ = {};
    RECT previewMenuTitleRect_ = {};
    RECT previewLogCloseRect_ = {};
    std::vector<RECT> previewUiButtonRects_;
    RECT inspectorCommitRect_ = {};
    RECT inspectorCancelRect_ = {};
    RECT leftSplitterRect_ = {};
    RECT rightSplitterRect_ = {};
    RECT graphSplitterRect_ = {};
    RECT eventSplitterRect_ = {};
    RECT currentEventListRect_ = {};
    RECT currentInspectorRect_ = {};
    RECT eventSearchRect_ = {};
    RECT inspectorEditRect_ = {};
    RECT eventTextEditRect_ = {};
    RECT lastClientRect_ = { 0, 0, 1280, 720 };
    size_t selectedCommandIndex_ = 0;
    size_t selectedChoiceLinkIndex_ = 0;
    size_t expandedTextCommandIndex_ = static_cast<size_t>(-1);
    size_t dragInsertIndex_ = 0;
    size_t eventDragSourceIndex_ = static_cast<size_t>(-1);
    size_t eventDragInsertIndex_ = static_cast<size_t>(-1);
    size_t assetDragSourceIndex_ = static_cast<size_t>(-1);
    size_t assetDropTargetIndex_ = static_cast<size_t>(-1);
    int leftPanelWidth_ = 280;
    int rightPanelWidth_ = 320;
    int graphHeight_ = 162;
    int eventListHeight_ = 208;
    int eventListScrollOffset_ = 0;
    int componentScrollOffset_ = 0;
    int componentScrollMax_ = 0;
    int inspectorScrollOffset_ = 0;
    int inspectorScrollMax_ = 0;
    int settingsScrollOffset_ = 0;
    int settingsScrollMax_ = 0;
    DragHandle activeDragHandle_ = DragHandle::None;
    size_t editingCommandIndex_ = 0;
    std::wstring editingKey_;
    std::wstring editingLabel_;
    std::wstring editingBuffer_;
    std::wstring eventFilterText_;
    std::wstring materialFilterText_;
    std::wstring scenarioFilterText_;
    std::wstring selectedAssetPath_;
    std::wstring selectedAssetLabel_;
    std::wstring selectedAssetPreviewCategory_;
    int assetPreviewVolume_ = 100;
    std::wstring selectedScenePath_;
    std::vector<std::wstring> backlogEntries_;
    std::vector<UiButtonDefinition> uiButtons_;
    std::vector<EditorSnapshot> undoStack_;
    std::vector<EditorSnapshot> redoStack_;
    DWORD waitUntilTick_ = 0;
    DWORD fadeStartTick_ = 0;
    DWORD fadeEndTick_ = 0;
    COLORREF fadeColor_ = RGB(0, 0, 0);
    int fadeOpacity_ = 255;
    std::wstring fadeTarget_ = L"stage";
    DWORD shakeEndTick_ = 0;
    int shakePower_ = 0;
    DWORD flashStartTick_ = 0;
    DWORD flashEndTick_ = 0;
    COLORREF flashColor_ = RGB(255, 255, 255);
    int flashOpacity_ = 220;
    COLORREF tintColor_ = RGB(255, 255, 255);
    int tintOpacity_ = 0;
    int stageOffsetX_ = 0;
    int stageOffsetY_ = 0;
    int stageScale_ = 100;
    DWORD zoomStartTick_ = 0;
    DWORD zoomEndTick_ = 0;
    int zoomStartScale_ = 100;
    int zoomTargetScale_ = 100;
    DWORD panStartTick_ = 0;
    DWORD panEndTick_ = 0;
    int panStartX_ = 0;
    int panStartY_ = 0;
    int panTargetX_ = 0;
    int panTargetY_ = 0;
    bool showComponents_ = true;
    bool showInspector_ = true;
    bool showFlowGraph_ = false;
    bool showPreviewPanel_ = false;
    bool showEventList_ = true;
    bool inspectorEditing_ = false;
    bool paletteDragActive_ = false;
    bool paletteDropValid_ = false;
    bool eventReorderDragActive_ = false;
    bool eventReorderMoved_ = false;
    bool assetDragActive_ = false;
    bool assetDragMoved_ = false;
    bool autosaveRestoreChecked_ = false;
    bool restoringAutosave_ = false;
    bool previewMenuVisible_ = false;
    bool previewLogVisible_ = false;
    bool previewVisible_ = false;
    bool previewSkipMode_ = false;
    bool previewAutoMode_ = false;
    bool previewFullscreen_ = false;
    bool waitingForChoice_ = false;
    bool reachedEnd_ = false;
    bool playerMode_ = false;
    bool characterAdjustMode_ = false;
    bool characterAdjustDragging_ = false;
    bool sceneDialogVisible_ = false;
    bool projectDialogVisible_ = false;
    bool projectLauncherVisible_ = false;
    bool characterManagerVisible_ = false;
    bool characterFieldDialogVisible_ = false;
    bool variableManagerVisible_ = false;
    bool variableFieldDialogVisible_ = false;
    bool settingsDialogVisible_ = false;
    LeftPanelTab leftPanelTab_ = LeftPanelTab::Components;
    std::wstring selectedAssetCategory_ = L"background";
    size_t adjustCharacterCommandIndex_ = static_cast<size_t>(-1);
    POINT adjustDragStartPoint_ = {};
    POINT lastMousePoint_ = {};
    int adjustStartX_ = 0;
    int adjustStartY_ = 0;
    int hoveredToolbarIndex_ = -1;
    DWORD autoAdvanceTick_ = 0;
    WINDOWPLACEMENT windowedPlacement_ = { sizeof(WINDOWPLACEMENT) };
    DWORD windowedStyle_ = 0;
    int hoveredPreviewUiButtonIndex_ = -1;
    size_t selectedCharacterDefinitionIndex_ = static_cast<size_t>(-1);
    size_t selectedVariableDefinitionIndex_ = static_cast<size_t>(-1);
    ScriptCommand::Type draggedPaletteType_ = ScriptCommand::Type::Text;
    std::wstring draggedPaletteLabel_;
    POINT dragPoint_ = {};
    POINT eventDragStartPoint_ = {};
    ScriptCommand copiedCommand_;
    bool hasCopiedCommand_ = false;
    std::wstring characterFieldDialogTitle_;
    std::wstring characterFieldDialogAction_;
    std::wstring variableFieldDialogTitle_;
    std::wstring variableFieldDialogAction_;
    EditorSettings editorSettings_;
    std::vector<CharacterDefinition> characterDefinitions_;
    std::vector<CharacterManagerActionTarget> characterManagerActionTargets_;
    std::vector<SettingsActionTarget> settingsActionTargets_;
    std::vector<RECT> settingsCategoryRects_;
    std::vector<RECT> characterDefinitionRects_;
    size_t selectedSettingsCategoryIndex_ = 0;
};
