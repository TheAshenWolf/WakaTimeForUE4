#pragma once

#include <string>
#include <Runtime/SlateCore/Public/Styling/SlateStyle.h>
#include "EditorStyleSet.h"

DECLARE_LOG_CATEGORY_EXTERN(LogWakaTime, Log, All);

class FWakaTimeForUEModule : public IModuleInterface
{
public:
	// Module methods


	/// <summary>
	///	Called when the module is initiated, in this case PostEngineInit
	/// </summary>
	virtual void StartupModule() override;

	/// <summary>
	///	Called when the module is being destroyed (usually when the engine is closed)
	/// </summary>
	virtual void ShutdownModule() override;


	// Initialization methods


	/// <summary>
	///	Assigns global variables like User home path, processor architecture and wakatime exe name
	/// </summary>
	void AssignGlobalVariables();

	/// <summary>
	///	Checks whether the wakatime config file exists and includes the api_key
	/// </summary>
	/// <param name="ConfigFilePath"> Path to the config file directory</param>
	void HandleStartupApiCheck(std::string ConfigFilePath);

	/// <summary>
	///	Checks whether the wakatime config file exists and includes the api_key
	/// </summary>
	/// <param name="ConfigFilePath"> Path to the config file directory</param>
	void ReadConfig(std::string ConfigFilePath, bool& bFoundApiKey, bool& bFoundApiUrl);

	/// <summary>
	///	Checks if Wakatime exists, if not, downloads it using Powershell
	/// </summary>
	/// <param name="CliPath"> Path to the wakatime exe file </param>
	void DownloadWakatimeCli(std::string CliPath);

	/// <summary>
	///	Returns the name of the project
	/// </summary>
	std::string GetProjectName();


	// UI methods


	/// <summary>
	///	Creates the wakatime icon which will reside in the toolbar
	/// </summary>
	TSharedRef<FSlateStyleSet> CreateToolbarIcon();

	/// <summary>
	///	Adds the created icon to the toolbar
	/// </summary>
	/// <param name="Builder"> The builder utility </param>
	void AddToolbarButton(FToolBarBuilder& Builder);

	/// <summary>
	///	Called when the toolbar icon is clicked; Reads config and opens the Slate window
	/// </summary>
	void OpenSettingsWindowFromUI();
	
	/// <summary>
	///	Opens the Slate window
	/// </summary>
	void OpenSettingsWindow();

	/// <summary>
	///	Called when user clicks "Save" within the slate window.
	///	Saves the entered api key into the wakatime.cfg file
	/// </summary>
	FReply SaveData();

	bool UpdateIniEntry(TMap<FString, FString>& Data, FString Key, FString Value);


	// Lifecycle methods


	/// <summary>
	///	Sends heartbeat to wakatime
	/// </summary>
	/// <param name="bFileSave"> whether to attach the file that is being worked on </param>
	/// <param name="FilePath"> path to the current file that is being edited </param>
	/// <param name="Activity"> activity being performed by the user while sending the heartbeat; e.g. coding, designing, debugging, etc. </param>
	void SendHeartbeat(bool bFileSave, std::string Activity, std::string EntityType, FString Entity, std::string Language);


	// Event methods

	/// <summary>
	///	Event called when an actor is dropped into the scene
	/// </summary>
	void OnNewActorDropped(const TArray<UObject*>& Objects, const TArray<AActor*>& Actors);

	/// <summary>
	///	Event called when CTRL+W is pressed (duplicate selected actors)
	/// </summary>
	void OnDuplicateActorsEnd();

	/// <summary>
	///	Event called when an actor is deleted
	/// </summary>
	void OnDeleteActorsEnd();

	/// <summary>
	///	Event called when a new level is added to the world
	/// </summary>
	void OnAddLevelToWorld(ULevel* Level);

	/// <summary>
	///	Event called when the world is saved (Generally CTRL + S while in the viewport window)
	/// </summary>
#if ENGINE_MAJOR_VERSION == 5
	void OnPostSaveWorld(UWorld* World, FObjectPostSaveContext Context);
#else
	void OnPostSaveWorld(uint32 SaveFlags, UWorld* World, bool bSucces);
#endif

	/// <summary>
	///	Event called after clicking play in the editor
	/// </summary>
	void OnPostPieStarted(bool bIsSimulating);

	/// <summary>
	///	Event called after ending the play session within the editor, either by using the stop button or hitting the ESC key
	/// </summary>
	void OnPrePieEnded(bool bIsSimulating);

	/// <summary>
	///	Event called prior to blueprint compiling
	/// </summary>
	void OnBlueprintPreCompile(UBlueprint* Blueprint);
	
	/// <summary>
	///	Event called when editor window is initialized
	/// </summary>
	void OnEditorInitialized(double TimeToInitializeEditor);

	/// <summary>
	///	Event called when asset window is opened
	/// </summary>
	void OnAssetOpened(UObject* Asset, IAssetEditorInstance* AssetEditor);

	/// <summary>
	///	Event called when asset window is closed
	/// </summary>
	void OnAssetClosed(UObject* Asset, IAssetEditorInstance* AssetEditor);

	TSharedPtr<FUICommandList> PluginCommands;
	TArray<TSharedRef<FString>> OpenedBPs;
};

class FWakaCommands : public TCommands<FWakaCommands>
{
public:
	FWakaCommands() : TCommands<FWakaCommands>(
		TEXT("WakatimeEditor"),
		NSLOCTEXT("Wakatime", "WakatimeEditor", "Wakatime Plugin"),
		NAME_None,
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 1
		FAppStyle::GetAppStyleSetName()
#else // TheAshenWolf(GetStyleSetName is deprecated as of UE5.1)
		FEditorStyle::GetStyleSetName()
#endif
	) { }

	/// <summary>
	///	Registers the click action on the toolbar button
	/// </summary>
	virtual void RegisterCommands() override;


	TSharedPtr<FUICommandInfo> WakaTimeSettingsCommand;
};
