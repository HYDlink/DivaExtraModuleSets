# 组装玩家安装所需文件、说明与许可证；使用显式允许清单防止源码或调试文件混入。
param(
  [string]$BuildDirectory='build/release'
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $projectRoot
$buildRoot=[IO.Path]::GetFullPath($BuildDirectory,$projectRoot)
$releaseRoot=Join-Path $projectRoot 'artifacts/release'
$modRoot=Join-Path $releaseRoot 'DivaExtraModuleSets'
if(Test-Path -LiteralPath $modRoot){
  $resolvedTarget=[IO.Path]::GetFullPath($modRoot)
  $expectedParent=[IO.Path]::GetFullPath($releaseRoot)
  if([IO.Path]::GetDirectoryName($resolvedTarget) -ne $expectedParent -or -not $resolvedTarget.StartsWith($projectRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){
    throw '发行暂存目录超出本项目，已拒绝清理。'
  }
  Remove-Item -LiteralPath $resolvedTarget -Recurse -Force
}
New-Item -ItemType Directory -Force $modRoot,(Join-Path $modRoot 'licenses') | Out-Null

$dll=Join-Path $buildRoot 'DivaExtraModuleSets.dll'
if(-not (Test-Path -LiteralPath $dll)){ throw '缺少已构建文件：DivaExtraModuleSets.dll' }
Copy-Item -LiteralPath $dll -Destination $modRoot -Force
Copy-Item -LiteralPath config/config.toml -Destination (Join-Path $modRoot 'config.toml') -Force
Copy-Item -LiteralPath settings.ini,LICENSE,THIRD_PARTY_NOTICES.md -Destination $modRoot -Force

foreach($readme in @('README.zh-CN.txt','README.en.txt')){
  $readmeText=[IO.File]::ReadAllText((Join-Path $projectRoot "config/$readme")).Replace("`r`n","`n").Replace("`n","`r`n")
  [IO.File]::WriteAllText((Join-Path $modRoot $readme),$readmeText,[Text.UTF8Encoding]::new($true))
}

Copy-Item -LiteralPath third_party/vendor/imgui/LICENSE.txt -Destination (Join-Path $modRoot 'licenses/ImGui.txt') -Force
Copy-Item -LiteralPath third_party/vendor/detours-source/LICENSE.md -Destination (Join-Path $modRoot 'licenses/Detours.md') -Force
Copy-Item -LiteralPath third_party/vendor/licenses/DivaModLoader.md -Destination (Join-Path $modRoot 'licenses/DivaModLoader.md') -Force
Copy-Item -LiteralPath third_party/vendor/licenses/toml++.txt -Destination (Join-Path $modRoot 'licenses/toml++.txt') -Force
Copy-Item -LiteralPath third_party/vendor/licenses/NewClassics.txt -Destination (Join-Path $modRoot 'licenses/NewClassics.txt') -Force
Copy-Item -LiteralPath third_party/vendor/licenses/ReDIVA.txt -Destination (Join-Path $modRoot 'licenses/ReDIVA.txt') -Force
Copy-Item -LiteralPath third_party/vendor/licenses/QuantumSwitch.txt,third_party/vendor/licenses/MikuMikuSwitchPlugin.txt -Destination (Join-Path $modRoot 'licenses') -Force

$allowed=@('DivaExtraModuleSets.dll','config.toml','settings.ini','README.zh-CN.txt','README.en.txt','LICENSE','THIRD_PARTY_NOTICES.md','licenses/ImGui.txt','licenses/Detours.md','licenses/DivaModLoader.md','licenses/toml++.txt','licenses/NewClassics.txt','licenses/ReDIVA.txt','licenses/QuantumSwitch.txt','licenses/MikuMikuSwitchPlugin.txt')
$actual=@(Get-ChildItem -LiteralPath $modRoot -Recurse -File | ForEach-Object {[IO.Path]::GetRelativePath($modRoot,$_.FullName).Replace('\','/')})
if(@(Compare-Object $allowed $actual).Count){ throw '安装包文件与允许清单不一致，已拒绝打包。' }

$zip=Join-Path $releaseRoot 'DivaExtraModuleSets-0.1.2-win64.zip'
Compress-Archive -LiteralPath $modRoot -DestinationPath $zip -Force
$utf8=[Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText(($zip+'.sha256'),(Get-FileHash $zip -Algorithm SHA256).Hash+'  '+[IO.Path]::GetFileName($zip)+"`n",$utf8)
Write-Output "玩家安装包已生成：$zip"
Write-Output "安装包 SHA-256：$((Get-FileHash $zip -Algorithm SHA256).Hash)"
