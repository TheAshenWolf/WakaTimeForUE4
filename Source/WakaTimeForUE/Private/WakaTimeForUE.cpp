// ReSharper disable CppParameterMayBeConst
// ReSharper disable CppLocalVariableMayBeConst
#include "WakaTimeForUE.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include "GeneralProjectSettings.h"
#include "LevelEditor.h"
#include "WakaTimeHelpers.h"
#include "Styling/SlateStyleRegistry.h"
#include <Editor/MainFrame/Public/Interfaces/IMainFrameModule.h>
#include <activation.h>
#include <fstream>

#include "BlueprintEditorModule.h"
#include "Interfaces/IPluginManager.h"
#include "UObject/ObjectSaveContext.h"

using namespace std;

#define LOCTEXT_NAMESPACE "FWakaTimeForUEModule"

// Global variables
string GAPIKey("");
string GAPIUrl("");
string GBaseCommand("");
string GUserProfile;
string GProjectPath;
string GPluginVersion;
string GWakatimeArchitecture;
string GWakaCliVersion;

// Handles
FDelegateHandle NewActorsDroppedHandle;
FDelegateHandle DeleteActorsEndHandle;
FDelegateHandle DuplicateActorsEndHandle;
FDelegateHandle AddLevelToWorldHandle;
FDelegateHandle PostSaveWorldHandle;
FDelegateHandle GPostPieStartedHandle;
FDelegateHandle GPrePieEndedHandle;
FDelegateHandle OnBlueprintPreCompileHandle;
FDelegateHandle OnEditorInitializedHandle;
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 4
FDelegateHandle OnAssetOpenedInEditorHandle;
FDelegateHandle OnAssetClosedInEditorHandle;
#endif

// UI Elements
TSharedRef<SEditableTextBox> GAPIKeyBlock = SNew(SEditableTextBox)
.Text(FText::FromString(FString(UTF8_TO_TCHAR(GAPIKey.c_str())))).MinDesiredWidth(500);
TSharedRef<SEditableTextBox> GAPIUrlBlock = SNew(SEditableTextBox)
.Text(FText::FromString(FString(UTF8_TO_TCHAR(GAPIUrl.c_str())))).MinDesiredWidth(500);
TSharedRef<SWindow> SettingsWindow = SNew(SWindow);
TSharedPtr<FSlateStyleSet> StyleSetInstance = nullptr;


DEFINE_LOG_CATEGORY(LogWakaTime);

// Module methods
void FWakaTimeForUEModule::StartupModule()
{
	AssignGlobalVariables();

	FString WakatimeCliFilePath = FString(GUserProfile.c_str()) + TEXT("\\.wakatime\\") + FString(GWakaCliVersion.c_str());
	
	// testing for "wakatime-cli.exe" which is used by most IDEs
	if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*WakatimeCliFilePath))
	{
		UE_LOG(LogWakaTime, Log, TEXT("Found IDE wakatime-cli"));
		GBaseCommand = (string(GUserProfile) + "\\.wakatime\\" + GWakaCliVersion);
	}
	else
	{
		// neither way was found; download and install the new version
		UE_LOG(LogWakaTime, Log, TEXT("Did not find wakatime"));
		GBaseCommand = "/c start \"\" /b \"" + string(GUserProfile) + "\\.wakatime\\" +
			GWakaCliVersion + "\"";
		string FolderPath = string(GUserProfile) + "\\.wakatime";
		if (!FWakaTimeHelpers::PathExists(FolderPath))
		{
			FWakaTimeHelpers::RunCmdCommand("mkdir " + FolderPath, false, INFINITE);
		}
		DownloadWakatimeCli(string(GUserProfile) + "\\.wakatime\\wakatime-cli\\" + GWakaCliVersion);
	}
	// TheAshenWolf(Wakatime-cli.exe is not in the path by default, which is why we have to use the user path)


	if (!StyleSetInstance.IsValid())
	{
		StyleSetInstance = CreateToolbarIcon();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleSetInstance);
	}

	string ConfigFileDir = string(GUserProfile) + "\\.wakatime.cfg";
	HandleStartupApiCheck(ConfigFileDir);

	// Add Listeners
	NewActorsDroppedHandle = FEditorDelegates::OnNewActorsDropped.AddRaw(
		this, &FWakaTimeForUEModule::OnNewActorDropped);
	DeleteActorsEndHandle = FEditorDelegates::OnDeleteActorsEnd.AddRaw(this, &FWakaTimeForUEModule::OnDeleteActorsEnd);
	DuplicateActorsEndHandle = FEditorDelegates::OnDuplicateActorsEnd.AddRaw(
		this, &FWakaTimeForUEModule::OnDuplicateActorsEnd);
	AddLevelToWorldHandle = FEditorDelegates::OnAddLevelToWorld.AddRaw(this, &FWakaTimeForUEModule::OnAddLevelToWorld);

#if ENGINE_MAJOR_VERSION >= 5
	PostSaveWorldHandle = FEditorDelegates::PostSaveWorldWithContext.AddRaw(this, &FWakaTimeForUEModule::OnPostSaveWorld);
#else// TheAshenWolf(PostSaveWorld is deprecated as of UE5)
	PostSaveWorldHandle = FEditorDelegates::PostSaveWorld.AddRaw(this, &FWakaTimeForUEModule::OnPostSaveWorld);
#endif
	
	GPostPieStartedHandle = FEditorDelegates::PostPIEStarted.AddRaw(this, &FWakaTimeForUEModule::OnPostPieStarted);
	GPrePieEndedHandle = FEditorDelegates::PrePIEEnded.AddRaw(this, &FWakaTimeForUEModule::OnPrePieEnded);
	
	OnEditorInitializedHandle = FEditorDelegates::OnEditorInitialized.AddRaw(this, &FWakaTimeForUEModule::OnEditorInitialized);

	FWakaCommands::Register();

	PluginCommands = MakeShareable(new FUICommandList);

	// This handles the WakaTime settings button in the top bar
	PluginCommands->MapAction(
		FWakaCommands::Get().WakaTimeSettingsCommand,
		FExecuteAction::CreateRaw(this, &FWakaTimeForUEModule::OpenSettingsWindowFromUI)
	);

	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");


	TSharedPtr<FExtender> NewToolbarExtender = MakeShareable(new FExtender);

	NewToolbarExtender->AddToolBarExtension("Content",
	                                        EExtensionHook::Before,
	                                        PluginCommands,
	                                        FToolBarExtensionDelegate::CreateRaw(
		                                        this, &FWakaTimeForUEModule::AddToolbarButton));
	LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(NewToolbarExtender);
}

void FWakaTimeForUEModule::ShutdownModule()
{
	// Remove event handles
	FEditorDelegates::OnNewActorsDropped.Remove(NewActorsDroppedHandle);
	FEditorDelegates::OnDeleteActorsEnd.Remove(DeleteActorsEndHandle);
	FEditorDelegates::OnDuplicateActorsEnd.Remove(DuplicateActorsEndHandle);
	FEditorDelegates::OnAddLevelToWorld.Remove(AddLevelToWorldHandle);
#if ENGINE_MAJOR_VERSION >= 5
	FEditorDelegates::PostSaveWorldWithContext.Remove(PostSaveWorldHandle);
#else // TheAshenWolf(PostSaveWorld is deprecated as of UE5)
	FEditorDelegates::PostSaveWorld.Remove(PostSaveWorldHandle);
#endif
	FEditorDelegates::PostPIEStarted.Remove(GPostPieStartedHandle);
	FEditorDelegates::PrePIEEnded.Remove(GPrePieEndedHandle);
	
	if (GEditor)
	{
		GEditor->OnBlueprintPreCompile().Remove(OnBlueprintPreCompileHandle);

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 4
		if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
		{
			AssetEditorSubsystem->OnAssetOpenedInEditor().Remove(OnAssetOpenedInEditorHandle);
			AssetEditorSubsystem->OnAssetClosedInEditor().Remove(OnAssetClosedInEditorHandle);
		}
#endif
	}
}

void FWakaCommands::RegisterCommands()
{
	UI_COMMAND(WakaTimeSettingsCommand, "Waka Time", "Waka time settings", EUserInterfaceActionType::Button,
	           FInputChord());
}


// Initialization methods
void FWakaTimeForUEModule::AssignGlobalVariables()
{
	// use _dupenv_s instead of getenv, as it is safer
	GUserProfile = "c:";
	size_t LenDrive = 0;
	char* envValue = nullptr;

	// Safely retrieve the USERPROFILE environment variable
	_dupenv_s(&envValue, &LenDrive, "USERPROFILE");

	// Check if the environment variable was retrieved successfully
	if (envValue != nullptr) {
		GUserProfile = envValue;  // Assign the value to GUserProfile
		free(envValue);           // Free the allocated memory to prevent memory leaks
	}

	WCHAR BufferW[256];
	GWakatimeArchitecture = GetSystemWow64DirectoryW(BufferW, 256) == 0 ? "386" : "amd64";
	GWakaCliVersion = "wakatime-cli-windows-" + GWakatimeArchitecture + ".exe";
	
	GProjectPath = TCHAR_TO_UTF8(*FPaths::ProjectDir().Replace(TEXT("/"), TEXT("\\")));
	
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("WakaTimeForUE"));
	GPluginVersion = TCHAR_TO_UTF8(*Plugin.Get()->GetDescriptor().VersionName);
}

void FWakaTimeForUEModule::HandleStartupApiCheck(string ConfigFilePath)
{
	if (!FWakaTimeHelpers::PathExists(ConfigFilePath))
	// if there is no .wakatime.cfg, open the settings window straight up
	{
		OpenSettingsWindow();
		return;
	}

	bool bFoundApiKey = false;
	bool bFoundApiUrl = false;
	ReadConfig(ConfigFilePath, bFoundApiKey, bFoundApiUrl);

	if (!bFoundApiKey)
	{
		UE_LOG(LogWakaTime, Warning, TEXT("API key not found in config file"));
		OpenSettingsWindow(); // if key was not found, open the settings
	}

	if (!bFoundApiUrl)
	{
		UE_LOG(LogWakaTime, Warning, TEXT("API url not found in config file"));
	}
}

void FWakaTimeForUEModule::ReadConfig(string ConfigFilePath, bool& bFoundApiKey, bool& bFoundApiUrl)
{
	string Line;
	
	fstream ConfigFile(ConfigFilePath);

	while (getline(ConfigFile, Line))
	{
		if (Line.find("api_key") != string::npos)
		{
			GAPIKey = Line.substr(Line.find(" = ") + 3); // Pozitrone(Extract only the api key from the line);
			GAPIKeyBlock.Get().SetText(FText::FromString(FString(UTF8_TO_TCHAR(GAPIKey.c_str()))));
			bFoundApiKey = true;
		}

		if (Line.find("api_url") != string::npos)
		{
			GAPIUrl = Line.substr(Line.find(" = ") + 3); // Pozitrone(Extract only the url key from the line);
			GAPIUrlBlock.Get().SetText(FText::FromString(FString(UTF8_TO_TCHAR(GAPIUrl.c_str()))));
			bFoundApiUrl = true;
		}
	}

	if(!bFoundApiKey)
	{
		GAPIKeyBlock.Get().SetText(FText::GetEmpty());
	}

	if(!bFoundApiUrl)
	{
		GAPIUrlBlock.Get().SetText(FText::GetEmpty());
	}

	ConfigFile.close();
}

void FWakaTimeForUEModule::DownloadWakatimeCli(string CliPath)
{
	if (FWakaTimeHelpers::PathExists(CliPath))
	{
		UE_LOG(LogWakaTime, Log, TEXT("CLI found"));
		return; // if CLI exists, no need to change anything
	}

	UE_LOG(LogWakaTime, Log, TEXT("CLI not found, attempting download."));

	string URL = "https://github.com/wakatime/wakatime-cli/releases/latest/download/wakatime-cli-windows-" +
		GWakatimeArchitecture + ".zip";

	// Reference to the local path where the zip file will be downloaded (Under Name)
	string LocalZipFilePath = string(GUserProfile) + "/.wakatime/" + "wakatime-cli.zip";

	bool bSuccessDownload = FWakaTimeHelpers::DownloadFile(URL, LocalZipFilePath);

	// Update the user about the new download (Success / Failure)
	if (bSuccessDownload)
	{
		UE_LOG(LogWakaTime, Log, TEXT("Successfully downloaded wakatime-cli.zip"));
		bool bSuccessUnzip = FWakaTimeHelpers::UnzipArchive(LocalZipFilePath,
		                                                    string(GUserProfile) + "\\.wakatime");

		if (bSuccessUnzip) UE_LOG(LogWakaTime, Log, TEXT("Successfully extracted wakatime-cli."));
	}
	else
	{
		UE_LOG(LogWakaTime, Error, TEXT("Error downloading wakatime-cli. Please, install it manually."));
	}
}

string FWakaTimeForUEModule::GetProjectName()
{
	const TCHAR* ProjectName = FApp::GetProjectName();
	string MainModuleName = TCHAR_TO_UTF8(ProjectName);
	const UGeneralProjectSettings& ProjectSettings = *GetDefault<UGeneralProjectSettings>();
	if (ProjectSettings.ProjectName != "")
	{
		return TCHAR_TO_UTF8(*(ProjectSettings.ProjectName));
	}

	if (MainModuleName != "")
	{
		return TCHAR_TO_UTF8(ProjectName);
	}

	return "Unreal Engine";
}


// UI methods
TSharedRef<FSlateStyleSet> FWakaTimeForUEModule::CreateToolbarIcon()
{
	TSharedRef<FSlateStyleSet> Style = MakeShareable(new FSlateStyleSet("WakaTime2DStyle"));

	FString ResourcesDirectory = IPluginManager::Get().FindPlugin(TEXT("WakaTimeForUE"))->GetBaseDir() + "/Resources";

	Style->SetContentRoot(ResourcesDirectory);
	Style->Set("mainIcon", new FSlateImageBrush(ResourcesDirectory + "/Icon128.png", FVector2D(40, 40),
	                                            FSlateColor(FLinearColor(1, 1, 1))));

	return Style;
}

void FWakaTimeForUEModule::AddToolbarButton(FToolBarBuilder& Builder)
{
	FSlateIcon Icon = FSlateIcon(TEXT("WakaTime2DStyle"), "mainIcon"); //Style.Get().GetStyleSetName(), "Icon128.png");

	Builder.AddToolBarButton(FWakaCommands::Get().WakaTimeSettingsCommand, NAME_None, FText::FromString("WakaTime"),
	                         FText::FromString("WakaTime plugin settings"),
	                         Icon, NAME_None);
}

void FWakaTimeForUEModule::OpenSettingsWindowFromUI()
{
	bool bFoundApiKey = false;
	bool bFoundApiUrl = false;
	ReadConfig(string(GUserProfile) + "\\.wakatime.cfg", bFoundApiKey, bFoundApiUrl);

	OpenSettingsWindow();
}

void FWakaTimeForUEModule::OpenSettingsWindow()
{
	SettingsWindow = SNew(SWindow)
		.Title(FText::FromString(TEXT("WakaTime Settings")))
		.ClientSize(FVector2D(800, 400))
		.SupportsMaximize(false)
		.SupportsMinimize(false).IsTopmostWindow(true)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		  .HAlign(HAlign_Center)
		  .VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			  .HAlign(HAlign_Left)
			  .VAlign(VAlign_Top)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Your api key:"))).MinDesiredWidth(500)
			]
			+ SVerticalBox::Slot()
			  .HAlign(HAlign_Center)
			  .VAlign(VAlign_Center)
			[
				GAPIKeyBlock
			]
			+ SVerticalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Top)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Your api url:"))).MinDesiredWidth(500)
			]
			+ SVerticalBox::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
			[
				GAPIUrlBlock
			]
		]
		+ SVerticalBox::Slot()
		  .HAlign(HAlign_Center)
		  .VAlign(VAlign_Bottom)
		[
			SNew(SBox).WidthOverride(100)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Save")))
		.OnClicked(FOnClicked::CreateRaw(this, &FWakaTimeForUEModule::SaveData))
			]
		]
	];
	IMainFrameModule& MainFrameModule =
		FModuleManager::LoadModuleChecked<IMainFrameModule>(TEXT
			("MainFrame"));
	if (MainFrameModule.GetParentWindow().IsValid())
	{
		FSlateApplication::Get().AddWindowAsNativeChild
		(SettingsWindow, MainFrameModule.GetParentWindow()
		                                .ToSharedRef());
	}
	else
	{
		FSlateApplication::Get().AddWindow(SettingsWindow);
	}
}

FReply FWakaTimeForUEModule::SaveData()
{
	GAPIKey = TCHAR_TO_UTF8(*(GAPIKeyBlock.Get().GetText().ToString()));
	GAPIUrl = TCHAR_TO_UTF8(*(GAPIUrlBlock.Get().GetText().ToString()));

	string ConfigFileDir = string(GUserProfile) + "/.wakatime.cfg";
	fstream ConfigFile(ConfigFileDir);

	if (!FWakaTimeHelpers::PathExists(ConfigFileDir) || ConfigFile.fail())
	{
		ConfigFile.open(ConfigFileDir, fstream::out);
		// Pozitrone(Create the file if it does not exist) and write the data in it
		ConfigFile << "[settings]" << '\n';
		ConfigFile << "api_key = " << GAPIKey;
		if(!GAPIUrl.empty())
		{
			ConfigFile << "api_url = " << GAPIUrl;
		}
		ConfigFile.close();

		SettingsWindow.Get().RequestDestroyWindow();
		return FReply::Handled();
	}

	map<string, string> Data;
	
	string TempLine;
	while (getline(ConfigFile, TempLine)) // RedKitsune(Turn entire ini file into TMap);
	{
		if(TempLine.find("[settings]") != string::npos) continue;

		size_t Pos = TempLine.find(" = ");
		string Key = TempLine.substr(0, Pos);
		string Value = TempLine.substr(Pos + std::strlen(" = "));

		Data.insert(std::make_pair(Key, Value));
	}
	ConfigFile.close();

	bool bIsDirty = false;
	
	UpdateIniEntry(bIsDirty, Data, "api_key", GAPIKey);
	UpdateIniEntry(bIsDirty, Data, "api_url", GAPIUrl);
	
	if(bIsDirty)
	{
		UE_LOG(LogWakaTime, Log, TEXT("Saving settings"));
		
		ConfigFile.open(ConfigFileDir, fstream::out);

		ConfigFile << "[settings]" << '\n';
		
		for(auto kvp : Data)
		{
			ConfigFile << kvp.first << " = " << kvp.second << '\n';
		}

		ConfigFile.close();
	}

	SettingsWindow.Get().RequestDestroyWindow();
	return FReply::Handled();
}

void FWakaTimeForUEModule::UpdateIniEntry(bool& bIsDirty, map<string, string>& Data, string Key, string Value)
{
	if(Value.empty())
	{
		if(Data.contains(Key))
		{
			Data.erase(Key);
			bIsDirty =  true;
		}
	} else
	{
		if(!Data.contains(Key))
		{
			Data.insert(std::make_pair(Key, Value));
			bIsDirty =  true;
		} else if(!Data[Key].compare(Value))
		{
			Data[Key] = Value;
			bIsDirty =  true;
		}
	}
}

// Lifecycle methods
void FWakaTimeForUEModule::SendHeartbeat(bool bFileSave, string Activity, string EntityType, FString Entity, string Language)
{
	UE_LOG(LogWakaTime, Log, TEXT("Sending Heartbeat"));

	string Command = GBaseCommand;

	Command += " --config " + string(GUserProfile) + "\\.wakatime.cfg ";
	Command += "--log-file " + string(GUserProfile) + "\\.wakatime\\wakatime.log ";

	if(GAPIUrl != "")
	{
		Command += "--api-url " + GAPIUrl + " ";
	}

	Command += "--project \"" + GetProjectName() + "\" ";
	Command += "--project-folder " + GProjectPath + " ";
	string EntityStr = TCHAR_TO_UTF8(*Entity.Replace(TEXT("/"), TEXT("\\")));
	Command += "--entity " + EntityStr + " ";
	Command += "--entity-type \"" + EntityType + "\" ";
	Command += "--language \"" + Language + "\" ";
	Command += "--plugin \"unreal-wakatime/" + GPluginVersion + "\" ";
	Command += "--category " + Activity + " ";

	if (bFileSave)
	{
		Command += "--write";
	}

	bool bSuccess = false;
	try
	{
		bSuccess = FWakaTimeHelpers::RunPowershellCommand(Command, false, INFINITE, true);
	}
	catch (int Err)
	{
		UE_LOG(LogWakaTime, Warning, TEXT("%i"), Err);
	}

	//bool success = RunCommand(command, false, baseCommand,INFINITE, true);
	if (bSuccess)
	{
		UE_LOG(LogWakaTime, Log, TEXT("Heartbeat successfully sent."));
	}
	else
	{
		UE_LOG(LogWakaTime, Error, TEXT("Heartbeat couldn't be sent."));
		UE_LOG(LogWakaTime, Error, TEXT("Error code = %d"), GetLastError());
	}
}

// Event methods
void FWakaTimeForUEModule::OnNewActorDropped(const TArray<UObject*>& Objects, const TArray<AActor*>& Actors)
{
	SendHeartbeat(false, "designing", "app", "Unreal Editor", "Unreal Editor");
}

void FWakaTimeForUEModule::OnDuplicateActorsEnd()
{
	SendHeartbeat(false, "designing", "app", "Unreal Editor", "Unreal Editor");
}

void FWakaTimeForUEModule::OnDeleteActorsEnd()
{
	SendHeartbeat(false, "designing", "app", "Unreal Editor", "Unreal Editor");
}

void FWakaTimeForUEModule::OnAddLevelToWorld(ULevel* Level)
{
	SendHeartbeat(false, "designing", "app", "Unreal Editor", "Unreal Editor");
}

#if ENGINE_MAJOR_VERSION == 5
	void FWakaTimeForUEModule::OnPostSaveWorld(UWorld* World, FObjectPostSaveContext Context)
	{
		SendHeartbeat(true, "designing", "app", "Unreal Editor", "Unreal Editor");
}
#else
	void FWakaTimeForUEModule::OnPostSaveWorld(uint32 SaveFlags, UWorld* World, bool bSucces)
	{
		SendHeartbeat(true, "designing", "app", "Unreal Editor", "Unreal Editor");
	}
#endif

void FWakaTimeForUEModule::OnPostPieStarted(bool bIsSimulating)
{
	SendHeartbeat(false, "debugging", "app", "Unreal Editor", "Unreal Editor");
}

void FWakaTimeForUEModule::OnPrePieEnded(bool bIsSimulating)
{
	SendHeartbeat(true, "debugging", "app", "Unreal Editor", "Unreal Editor");
}

void FWakaTimeForUEModule::OnBlueprintPreCompile(UBlueprint* Blueprint)
{
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 4
	auto Found = OpenedBPs.ContainsByPredicate([Blueprint](const TSharedRef<FString>& BPName)
		{
			return *BPName == Blueprint->GetName();
		});
	
	if(!Found) return;

	FString PackageName = Blueprint->GetOutermost()->GetName();
	FString FilePath = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
 
	SendHeartbeat(true, "coding", "file", TCHAR_TO_UTF8(*FilePath), "Blueprints");
#else
	SendHeartbeat(true, "coding", "app", "Unreal Editor", "Blueprints");
#endif
}

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 4
void FWakaTimeForUEModule::OnAssetOpened(UObject* Asset, IAssetEditorInstance* AssetEditor)
{
	if(!Asset->IsA<UBlueprint>()) return;
	
	OpenedBPs.Add(MakeShared<FString>(Asset->GetName()));
}

void FWakaTimeForUEModule::OnAssetClosed(UObject* Asset, IAssetEditorInstance* AssetEditor)
{
	if(!Asset->IsA<UBlueprint>()) return;
	
	OpenedBPs.RemoveAll([Asset](const TSharedRef<FString>& BPName)
	{
		return *BPName == Asset->GetName();
	});
}
#endif

void FWakaTimeForUEModule::OnEditorInitialized(double TimeToInitializeEditor)
{
	if (GEditor)
	{
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 4
		if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
		{
			OnAssetOpenedInEditorHandle = AssetEditorSubsystem->OnAssetOpenedInEditor().AddRaw(this, &FWakaTimeForUEModule::OnAssetOpened);
			OnAssetClosedInEditorHandle = AssetEditorSubsystem->OnAssetClosedInEditor().AddRaw(this, &FWakaTimeForUEModule::OnAssetClosed);
		}
#endif
		
		OnBlueprintPreCompileHandle = GEditor->OnBlueprintPreCompile().AddRaw(this, &FWakaTimeForUEModule::OnBlueprintPreCompile);
	}
	else
	{
		UE_LOG(LogWakaTime, Error, TEXT("No GEditor present"));
	}

	FEditorDelegates::OnEditorInitialized.Remove(OnEditorInitializedHandle);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FWakaTimeForUEModule, WakaTimeForUE)

#include "Windows/HideWindowsPlatformTypes.h"
