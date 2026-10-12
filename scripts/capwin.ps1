param([int]$ProcId, [string]$Out)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class W {
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder s, int n);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  public struct RECT { public int L, T, R, B; }
}
"@
$wins = New-Object System.Collections.ArrayList
[W]::EnumWindows({ param($h,$l) $p=0; [W]::GetWindowThreadProcessId($h,[ref]$p) | Out-Null; if ($p -eq $ProcId -and [W]::IsWindowVisible($h)) { [void]$wins.Add($h) }; $true }, [IntPtr]::Zero) | Out-Null
foreach ($h in $wins) {
  $sb = New-Object System.Text.StringBuilder 256; [W]::GetWindowText($h,$sb,256) | Out-Null
  $r = New-Object W+RECT; [W]::GetClientRect($h,[ref]$r) | Out-Null
  $w=$r.R-$r.L; $hh=$r.B-$r.T; if ($w -lt 100) { continue }
  $bmp = New-Object System.Drawing.Bitmap $w, $hh; $g=[System.Drawing.Graphics]::FromImage($bmp); $hdc=$g.GetHdc()
  $ok = [W]::PrintWindow($h,$hdc,3); $g.ReleaseHdc($hdc); $g.Dispose()
  $bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
  "captured '$($sb.ToString())' ${w}x${hh} ok=$ok -> $Out"; break
}
