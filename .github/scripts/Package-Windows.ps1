[CmdletBinding()]
param(
    [ValidateSet('x64')]
    [string] $Target = 'x64',
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'RelWithDebInfo'
)

$ErrorActionPreference = 'Stop'

if ( $DebugPreference -eq 'Continue' ) {
    $VerbosePreference = 'Continue'
    $InformationPreference = 'Continue'
}

if ( $env:CI -eq $null ) {
    throw "Package-Windows.ps1 requires CI environment"
}

if ( ! ( [System.Environment]::Is64BitOperatingSystem ) ) {
    throw "Packaging script requires a 64-bit system to build and run."
}

if ( $PSVersionTable.PSVersion -lt '7.2.0' ) {
    Write-Warning 'The packaging script requires PowerShell Core 7. Install or upgrade your PowerShell version: https://aka.ms/pscore6'
    exit 2
}

function Package {
    trap {
        Write-Error $_
        exit 2
    }

    $ScriptHome = $PSScriptRoot
    $ProjectRoot = Resolve-Path -Path "$PSScriptRoot/../.."
    $BuildSpecFile = "${ProjectRoot}/buildspec.json"

    $UtilityFunctions = Get-ChildItem -Path $PSScriptRoot/utils.pwsh/*.ps1 -Recurse

    foreach( $Utility in $UtilityFunctions ) {
        Write-Debug "Loading $($Utility.FullName)"
        . $Utility.FullName
    }

    $BuildSpec = Get-Content -Path ${BuildSpecFile} -Raw | ConvertFrom-Json
    $ProductName = $BuildSpec.name
    $ProductDisplayName = if ($BuildSpec.displayName) { $BuildSpec.displayName } else { $BuildSpec.name }
    $ProductVersion = $BuildSpec.version

    $OutputName = "${ProductName}-${ProductVersion}-windows-${Target}"
    $ReleaseConfigDir = "${ProjectRoot}/release/${Configuration}"
    $OutputInstallerName = "${OutputName}-setup"

    $RemoveArgs = @{
        ErrorAction = 'SilentlyContinue'
        Path = @(
            "${ProjectRoot}/release/${ProductName}-*-windows-*.zip",
            "${ProjectRoot}/release/${ProductName}-*-windows-*.exe"
        )
    }

    Remove-Item @RemoveArgs

    if ( ! (Test-Path -Path $ReleaseConfigDir -PathType Container) ) {
        throw "No release directory found at '${ReleaseConfigDir}'. Build/install step may have failed."
    }

    # Resolve Inno Setup compiler (ISCC)
    $IsccPath = $null
    $IsccCommand = Get-Command iscc -ErrorAction SilentlyContinue
    if ( $IsccCommand ) {
        $IsccPath = $IsccCommand.Source
    } else {
        $DefaultIsccPath = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
        if ( Test-Path -Path $DefaultIsccPath -PathType Leaf ) {
            $IsccPath = $DefaultIsccPath
        }
    }

    if ( ! $IsccPath ) {
        throw "Inno Setup compiler (iscc) not found on runner."
    }

    $InstallerScriptPath = "${ProjectRoot}/release/windows-installer.iss"

    $InstallerScript = @"
[Setup]
AppId={{D3943C4F-2A9D-4A6E-94A2-E649B5E8CC91}
AppName=$ProductDisplayName
AppVersion=$ProductVersion
AppPublisher=NeuroMiner
DefaultDirName={code:GetObsPluginInstallDir}
UsePreviousAppDir=no
DefaultGroupName=$ProductDisplayName
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Compression=lzma2
SolidCompression=yes
OutputDir=$ProjectRoot\release
OutputBaseFilename=$OutputInstallerName
WizardStyle=modern
Uninstallable=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "$ReleaseConfigDir\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Run]
Filename: "{code:GetObsExePath}"; Description: "Launch OBS Studio"; Flags: postinstall skipifsilent nowait; Check: ObsExeExists

[Code]
function NormalizeDir(const Dir: string): string;
begin
  Result := Trim(Dir);
  if (Result <> '') and (Result[Length(Result)] = '\') then
    Result := Copy(Result, 1, Length(Result) - 1);
end;

function CandidateFromInstallLocation(const RootKey: Integer; const SubKey: string): string;
var
  InstallLocation: string;
begin
  Result := '';
  if RegQueryStringValue(RootKey, SubKey, 'InstallLocation', InstallLocation) then begin
    InstallLocation := NormalizeDir(InstallLocation);
    if DirExists(InstallLocation) then
      Result := InstallLocation;
  end;
end;

function CandidateFromUninstallString(const RootKey: Integer; const SubKey: string): string;
var
  UninstallString: string;
  CandidateExe: string;
  QuotePos: Integer;
begin
  Result := '';
  if not RegQueryStringValue(RootKey, SubKey, 'UninstallString', UninstallString) then
    exit;

  UninstallString := Trim(UninstallString);
  if UninstallString = '' then
    exit;

  if UninstallString[1] = '"' then begin
    Delete(UninstallString, 1, 1);
    QuotePos := Pos('"', UninstallString);
    if QuotePos > 0 then
      CandidateExe := Copy(UninstallString, 1, QuotePos - 1)
    else
      CandidateExe := UninstallString;
  end else begin
    QuotePos := Pos(' /', UninstallString);
    if QuotePos > 0 then
      CandidateExe := Copy(UninstallString, 1, QuotePos - 1)
    else
      CandidateExe := UninstallString;
  end;

  if FileExists(CandidateExe) then begin
    Result := NormalizeDir(ExtractFileDir(CandidateExe));
    if not DirExists(Result) then
      Result := '';
  end;
end;

function CandidateIfObsExeExists(const RootPath: string): string;
var
  CandidateRoot: string;
begin
  Result := '';
  CandidateRoot := NormalizeDir(RootPath);
  if CandidateRoot = '' then
    exit;
  if FileExists(AddBackslash(CandidateRoot) + 'bin\64bit\obs64.exe') then
    Result := CandidateRoot;
end;

function ProbeObsRootOnDrive(const DriveIndex: Integer): string;
var
  DriveRoot: string;
begin
  Result := '';
  DriveRoot := Chr(DriveIndex) + ':\';

  Result := CandidateIfObsExeExists(DriveRoot + 'Program Files\obs-studio');
  if Result <> '' then exit;
  Result := CandidateIfObsExeExists(DriveRoot + 'Program Files\OBS Studio');
  if Result <> '' then exit;
  Result := CandidateIfObsExeExists(DriveRoot + 'Program Files (x86)\obs-studio');
  if Result <> '' then exit;
  Result := CandidateIfObsExeExists(DriveRoot + 'Program Files (x86)\OBS Studio');
  if Result <> '' then exit;
end;

function FindObsInstallRoot: string;
var
  DriveIndex: Integer;
begin
  // System-wide uninstall entries
  Result := CandidateFromInstallLocation(HKLM64, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio');
  if Result <> '' then exit;
  Result := CandidateFromInstallLocation(HKLM64, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\obs-studio');
  if Result <> '' then exit;
  Result := CandidateFromInstallLocation(HKLM64, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio_is1');
  if Result <> '' then exit;
  Result := CandidateFromInstallLocation(HKLM64, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\obs-studio_is1');
  if Result <> '' then exit;

  Result := CandidateFromInstallLocation(HKLM, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio');
  if Result <> '' then exit;
  Result := CandidateFromInstallLocation(HKLM, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\obs-studio');
  if Result <> '' then exit;
  Result := CandidateFromInstallLocation(HKLM, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio_is1');
  if Result <> '' then exit;
  Result := CandidateFromInstallLocation(HKLM, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\obs-studio_is1');
  if Result <> '' then exit;

  Result := CandidateFromUninstallString(HKLM64, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio');
  if Result <> '' then exit;
  Result := CandidateFromUninstallString(HKLM64, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\obs-studio');
  if Result <> '' then exit;
  Result := CandidateFromUninstallString(HKLM64, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio_is1');
  if Result <> '' then exit;
  Result := CandidateFromUninstallString(HKLM64, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\obs-studio_is1');
  if Result <> '' then exit;

  Result := CandidateFromUninstallString(HKLM, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio');
  if Result <> '' then exit;
  Result := CandidateFromUninstallString(HKLM, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\obs-studio');
  if Result <> '' then exit;
  Result := CandidateFromUninstallString(HKLM, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio_is1');
  if Result <> '' then exit;
  Result := CandidateFromUninstallString(HKLM, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\obs-studio_is1');
  if Result <> '' then exit;

  // Per-user uninstall entries (e.g. user-scoped installs)
  Result := CandidateFromInstallLocation(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio');
  if Result <> '' then exit;
  Result := CandidateFromInstallLocation(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\obs-studio');
  if Result <> '' then exit;
  Result := CandidateFromInstallLocation(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio_is1');
  if Result <> '' then exit;
  Result := CandidateFromInstallLocation(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\obs-studio_is1');
  if Result <> '' then exit;

  Result := CandidateFromUninstallString(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio');
  if Result <> '' then exit;
  Result := CandidateFromUninstallString(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\obs-studio');
  if Result <> '' then exit;
  Result := CandidateFromUninstallString(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio_is1');
  if Result <> '' then exit;
  Result := CandidateFromUninstallString(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\obs-studio_is1');
  if Result <> '' then exit;

  // Fallback: scan common install locations on all drive letters.
  for DriveIndex := 67 to 90 do begin
    Result := ProbeObsRootOnDrive(DriveIndex);
    if Result <> '' then exit;
  end;

  Result := CandidateIfObsExeExists(ExpandConstant('{autopf64}\obs-studio'));
  if Result <> '' then exit;
  Result := CandidateIfObsExeExists(ExpandConstant('{autopf}\obs-studio'));
  if Result <> '' then exit;
  Result := CandidateIfObsExeExists(ExpandConstant('{autopf64}\OBS Studio'));
  if Result <> '' then exit;
  Result := CandidateIfObsExeExists(ExpandConstant('{autopf}\OBS Studio'));
  if Result <> '' then exit;
  Result := CandidateIfObsExeExists(ExpandConstant('{autopf32}\obs-studio'));
  if Result <> '' then exit;
  Result := CandidateIfObsExeExists(ExpandConstant('{autopf32}\OBS Studio'));
  if Result <> '' then exit;

  // Final fallback for custom/portable installs: keep common official default.
  Result := ExpandConstant('{autopf64}\obs-studio');
end;

function GetObsPluginInstallDir(Param: string): string;
begin
  Result := ExpandConstant('{commonappdata}\obs-studio\plugins');
end;

function GetObsExePath(Param: string): string;
begin
  Result := AddBackslash(FindObsInstallRoot()) + 'bin\64bit\obs64.exe';
end;

function ObsExeExists: Boolean;
begin
  Result := FileExists(GetObsExePath(''));
end;
"@

    Set-Content -Path $InstallerScriptPath -Value $InstallerScript -Encoding UTF8

    Log-Group "Building Windows installer ${OutputInstallerName}.exe..."
    & $IsccPath $InstallerScriptPath

    if ( $LASTEXITCODE -ne 0 ) {
        throw "Inno Setup failed with exit code $LASTEXITCODE."
    }

    if ( Test-Path -Path $InstallerScriptPath -PathType Leaf ) {
        Remove-Item -Path $InstallerScriptPath -Force -ErrorAction SilentlyContinue
    }
    Log-Group
}

Package
