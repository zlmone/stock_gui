<#
  stock_gui 一键下载最新发布包 (Windows PowerShell)
  用法:
    powershell -ExecutionPolicy Bypass -File download_latest.ps1
    powershell -ExecutionPolicy Bypass -File download_latest.ps1 -OutDir D:\stock_gui -Extract
#>
param(
  [string]$Base   = "http://10.168.1.242:3000",
  [string]$Repo   = "srdev/stock_gui",
  [string]$User   = "srdev",
  [string]$Pass   = "king2014",
  [string]$OutDir = ".",
  [switch]$Extract
)

$ErrorActionPreference = "Stop"

$auth    = [Convert]::ToBase64String([Text.Encoding]::ASCII.GetBytes("${User}:${Pass}"))
$headers = @{ Authorization = "Basic $auth" }

Write-Host "查询最新发布: $Base/api/v1/repos/$Repo/releases/latest"
$rel = Invoke-RestMethod -Uri "$Base/api/v1/repos/$Repo/releases/latest" -Headers $headers
if (-not $rel.assets -or $rel.assets.Count -eq 0) { throw "该 release 没有 asset" }

$asset = $rel.assets[0]
$out   = Join-Path $OutDir $asset.name
$mb    = [math]::Round($asset.size / 1MB, 1)
Write-Host "tag=$($rel.tag_name)  下载 $($asset.name) (约 $mb MB)"
Invoke-WebRequest -Uri $asset.browser_download_url -Headers $headers -OutFile $out
Write-Host "已保存: $out"

if ($Extract) {
  $dest = Join-Path $OutDir ([IO.Path]::GetFileNameWithoutExtension($asset.name))
  Write-Host "解压到: $dest"
  Expand-Archive -Path $out -DestinationPath $dest -Force
  Write-Host "完成。双击 $dest\start.bat 运行。"
}
