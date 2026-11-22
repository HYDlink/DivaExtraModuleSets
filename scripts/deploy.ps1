# 部署 DivaExtraModuleSets 到已安装游戏目录，并在加载列表末尾追加启用。
param(
  [Parameter(Mandatory=$true)]
  [string]$GameDirectory
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $projectRoot
if(Get-Process DivaMegaMix -ErrorAction SilentlyContinue){ throw '游戏正在运行，已停止部署以避免替换已加载的 DLL；未关闭游戏进程。' }

$gameRoot=[IO.Path]::GetFullPath($GameDirectory)
$modsRoot=Join-Path $gameRoot 'mods'
$target=[IO.Path]::GetFullPath((Join-Path $modsRoot 'DivaExtraModuleSets'))
if([IO.Path]::GetDirectoryName($target) -ne [IO.Path]::GetFullPath($modsRoot)){ throw '目标目录校验失败。' }
if(-not (Test-Path -LiteralPath (Join-Path $gameRoot 'DivaMegaMix.exe'))){ throw '指定目录没有 DivaMegaMix.exe。' }

$source=Join-Path $projectRoot 'artifacts/release/DivaExtraModuleSets'
$configPath=Join-Path $gameRoot 'config.toml'
$original=[IO.File]::ReadAllText($configPath,[Text.Encoding]::UTF8)
$originalHash=(Get-FileHash -LiteralPath $configPath -Algorithm SHA256).Hash
$pattern='(?ms)^priority\s*=\s*\[(?<items>.*?)\]'
$matchesFound=[regex]::Matches($original,$pattern)
if($matchesFound.Count -ne 1){ throw '加载器 priority 数组不是唯一数组，已停止部署。' }
$match=$matchesFound[0]
$items=$match.Groups['items'].Value
$updated=$original
if($items -notmatch '"DivaExtraModuleSets"'){
  $newItems=$items.TrimEnd()
  if($newItems.Trim().Length -gt 0){ $newItems+=', ' }
  $newItems+='"DivaExtraModuleSets"'
  $replacement='priority = ['+$newItems+']'
  $updated=$original.Substring(0,$match.Index)+$replacement+$original.Substring($match.Index+$match.Length)
}

$timestamp=Get-Date -Format 'yyyyMMdd-HHmmss'
$backup=Join-Path $projectRoot "artifacts/deployment/$timestamp"
New-Item -ItemType Directory -Force $backup | Out-Null
Copy-Item -LiteralPath $configPath -Destination (Join-Path $backup 'config.toml.before')
[IO.File]::WriteAllText((Join-Path $backup 'config.toml.after'),$updated,[Text.UTF8Encoding]::new($false))

$files=@('DivaExtraModuleSets.dll','config.toml','settings.ini','README.zh-CN.txt','README.en.txt','LICENSE','THIRD_PARTY_NOTICES.md','licenses/ImGui.txt','licenses/Detours.md','licenses/DivaModLoader.md','licenses/toml++.txt','licenses/NewClassics.txt','licenses/ReDIVA.txt','licenses/QuantumSwitch.txt','licenses/MikuMikuSwitchPlugin.txt')
$stage=Join-Path $modsRoot "DivaExtraModuleSets.deploy-$timestamp"
New-Item -ItemType Directory -Path $stage | Out-Null
try{
  if(Test-Path -LiteralPath (Join-Path $target 'settings.ini')){
    Copy-Item -LiteralPath (Join-Path $target 'settings.ini') -Destination (Join-Path $backup 'settings.ini.before')
  }
  foreach($relative in $files){
    $inputFile=Join-Path $source $relative
    $outputFile=Join-Path $stage $relative
    New-Item -ItemType Directory -Force (Split-Path $outputFile -Parent) | Out-Null
    Copy-Item -LiteralPath $inputFile -Destination $outputFile
    if((Get-FileHash -LiteralPath $inputFile).Hash -ne (Get-FileHash -LiteralPath $outputFile).Hash){ throw "部署文件校验失败：$relative" }
  }
  if((Get-FileHash -LiteralPath $configPath).Hash -ne $originalHash){ throw '加载器配置在部署过程中发生变化，已停止修改。' }
  if(Test-Path -LiteralPath $target){ Remove-Item -LiteralPath $target -Recurse -Force }
  Move-Item -LiteralPath $stage -Destination $target
  if($updated -ne $original){ [IO.File]::WriteAllText($configPath,$updated,[Text.UTF8Encoding]::new($false)) }
  Write-Output "已部署并启用：$target"
  Write-Output "配置备份：$backup"
}catch{
  if([IO.File]::ReadAllText($configPath) -eq $updated -and $updated -ne $original){
    Copy-Item -LiteralPath (Join-Path $backup 'config.toml.before') -Destination $configPath -Force
  }
  throw
}
