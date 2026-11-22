# 检查玩家可见代码里没有硬编码中文；新增文案应写入 locales/*.toml。
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$sharedChecker=Join-Path (Split-Path $projectRoot -Parent) 'shared/tools/i18n/check-hardcoded-zh.ps1'
& $sharedChecker -Root $projectRoot -Files @('src/ui.cpp','src/settings.cpp','src/slot_store.cpp')
