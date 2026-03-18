param(
    [string[]]$Targets
)

$ErrorActionPreference = "Stop"

function New-TestProject {
    param(
        [Parameter(Mandatory = $true)]
        [string]$EngineVersion,

        [Parameter(Mandatory = $true)]
        [string]$ProjectName
    )

    $engineRoot = "C:\Program Files\Epic Games\UE_$EngineVersion"
    $templateRoot = Join-Path $engineRoot "Templates\TP_Blank"
    $projectRoot = "C:\Workspace\$ProjectName"
    $projectFile = Join-Path $projectRoot "$ProjectName.uproject"

    if (Test-Path $projectRoot) {
        Remove-Item $projectRoot -Recurse -Force
    }

    Copy-Item $templateRoot $projectRoot -Recurse -Force

    Rename-Item (Join-Path $projectRoot "TP_Blank.uproject") "$ProjectName.uproject"
    Rename-Item (Join-Path $projectRoot "Source\TP_Blank") $ProjectName
    Rename-Item (Join-Path $projectRoot "Source\TP_Blank.Target.cs") "$ProjectName.Target.cs"
    Rename-Item (Join-Path $projectRoot "Source\TP_BlankEditor.Target.cs") "${ProjectName}Editor.Target.cs"
    Rename-Item (Join-Path $projectRoot "Source\$ProjectName\TP_Blank.Build.cs") "$ProjectName.Build.cs"
    Rename-Item (Join-Path $projectRoot "Source\$ProjectName\TP_Blank.cpp") "$ProjectName.cpp"
    Rename-Item (Join-Path $projectRoot "Source\$ProjectName\TP_Blank.h") "$ProjectName.h"

    $filesToRewrite = Get-ChildItem $projectRoot -Recurse -File |
        Where-Object { $_.Extension -in ".cs", ".cpp", ".h", ".ini", ".uproject" }

    foreach ($file in $filesToRewrite) {
        $content = Get-Content $file.FullName -Raw -ErrorAction SilentlyContinue
        if ($null -eq $content) {
            $content = ""
        }

        $content = $content.Replace("TP_Blank", $ProjectName)
        $content = $content.Replace("TP_BLANK", $ProjectName.ToUpper())
        $content = $content.Replace("tp_blank", $ProjectName.ToLower())
        Set-Content $file.FullName $content -Encoding utf8
    }

    $uprojectJson = Get-Content $projectFile -Raw | ConvertFrom-Json
    $uprojectJson.EngineAssociation = $EngineVersion

    $pluginNames = @($uprojectJson.Plugins.Name)
    if ($pluginNames -notcontains "LiveLink") {
        $uprojectJson.Plugins += [pscustomobject]@{
            Name = "LiveLink"
            Enabled = $true
        }
    }
    if ($pluginNames -notcontains "MOVINLiveLink") {
        $uprojectJson.Plugins += [pscustomobject]@{
            Name = "MOVINLiveLink"
            Enabled = $true
        }
    }

    $uprojectJson | ConvertTo-Json -Depth 10 | Set-Content $projectFile -Encoding utf8

    $pluginsRoot = Join-Path $projectRoot "Plugins"
    $pluginDest = Join-Path $pluginsRoot "MOVINLiveLink"
    New-Item -ItemType Directory -Path $pluginsRoot -Force | Out-Null
    New-Item -ItemType Directory -Path $pluginDest -Force | Out-Null
    Copy-Item "C:\Workspace\MOVINLiveLinkPlugin\*" $pluginDest -Recurse -Force -Exclude "_build", ".git"

    Write-Output "Created $ProjectName"
}

if ($null -eq $Targets -or $Targets.Count -eq 0) {
    $Targets = @(
        "5.7=UE57LiveLinkTest",
        "5.6=UE56LiveLinkTest"
    )
}

foreach ($target in $Targets) {
    $parts = $target.Split("=", 2)
    if ($parts.Count -ne 2) {
        throw "Invalid target format: $target"
    }

    New-TestProject -EngineVersion $parts[0] -ProjectName $parts[1]
}
