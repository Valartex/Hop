param([Parameter(Mandatory)][Security.SecureString]$Key, [switch]$GuardFault)

$ErrorActionPreference = 'Stop'
$hopRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$hopJobs = Join-Path $hopRoot '.research\vm-exchange\HopJobs'
$hopNonce = [guid]::NewGuid().ToString('N')
$hopMarker = Join-Path $hopRoot 'build\windows-test\Hop-VM-Extra.json'
$hopInbox = Join-Path $hopJobs 'inbox.json'
if (Test-Path -LiteralPath $hopInbox) { throw 'A VM test job is already queued' }
$hopStatusPath = Join-Path $hopJobs 'agent-status.json'
$hopState = Get-Content -LiteralPath $hopStatusPath -Raw | ConvertFrom-Json
# Свежесть записи определяется часами хоста: часы гостя могут отставать после сна.
$hopStatusAge = ([DateTime]::UtcNow - (Get-Item -LiteralPath $hopStatusPath).LastWriteTimeUtc).TotalSeconds
if ($hopState.state -ne 'ready' -or $hopStatusAge -lt 0 -or $hopStatusAge -gt 15 -or
    [Math]::Abs(([DateTime]::UtcNow - ([DateTime]$hopState.updatedAtUtc).ToUniversalTime()).TotalSeconds) -gt 120) { throw 'The VM test agent is not ready' }
try {
    @{ kind='amnezia-awg'; nonce=$hopNonce; expires=[DateTime]::UtcNow.AddMinutes(5).ToString('yyyy-MM-ddTHH:mm:ss.fffZ') } |
        ConvertTo-Json -Compress | Set-Content -LiteralPath $hopMarker -Encoding utf8
    $hopJob = @{ id=[guid]::NewGuid().ToString('N'); kind=$(if ($GuardFault) { 'guard-fault' } else { 'baseline' }) }
    $hopJob | ConvertTo-Json -Compress | Set-Content (Join-Path $hopJobs 'inbox.tmp') -Encoding utf8
    Move-Item -LiteralPath (Join-Path $hopJobs 'inbox.tmp') -Destination $hopInbox
    # Агент сначала копирует комплект. Холодная проверка бинарников Windows может задержать запуск.
    $hopUntil = [DateTime]::UtcNow.AddSeconds(90)
    $hopPublicPath = Join-Path $hopJobs "$hopNonce-key.json"
    while (-not (Test-Path -LiteralPath $hopPublicPath)) {
        if (Test-Path -LiteralPath (Join-Path $hopJobs ($hopJob.id + '.json'))) {
            throw "The VM probe exited before key exchange; inspect job $($hopJob.id)"
        }
        if ([DateTime]::UtcNow -gt $hopUntil) { throw 'The guest did not publish an ephemeral public key' }
        Start-Sleep -Milliseconds 200
    }
    $hopPublic = Get-Content -LiteralPath $hopPublicPath -Raw | ConvertFrom-Json
    if ($hopPublic.nonce -ne $hopNonce) { throw 'Guest nonce mismatch' }
    if (-not ('HopVmKeyEnvelope' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Security.Cryptography;
using System.Text;
using System.Collections.Generic;
public static class HopVmKeyEnvelope {
    public static byte[] Encrypt(string publicPem, byte[] plain) {
        using (RSA rsa = RSA.Create()) {
            rsa.ImportFromPem(publicPem);
            return rsa.Encrypt(plain, RSAEncryptionPadding.OaepSHA256);
        }
    }
    public static Dictionary<string,string> Envelope(string publicPem, byte[] plain, string nonce) {
        byte[] key = RandomNumberGenerator.GetBytes(32);
        try {
            byte[] iv = RandomNumberGenerator.GetBytes(12);
            byte[] cipher = new byte[plain.Length];
            byte[] tag = new byte[16];
            using (AesGcm aes = new AesGcm(key, 16)) { aes.Encrypt(iv, plain, cipher, tag, Encoding.UTF8.GetBytes(nonce)); }
            return new Dictionary<string,string> {
                {"nonce", nonce}, {"encrypted", Convert.ToBase64String(Encrypt(publicPem,key))},
                {"iv", Convert.ToBase64String(iv)}, {"tag", Convert.ToBase64String(tag)}, {"body", Convert.ToBase64String(cipher)}
            };
        } finally { CryptographicOperations.ZeroMemory(key); }
    }
}
'@
    }
        $hopPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($Key)
        try { $hopPlain = [Text.Encoding]::UTF8.GetBytes([Runtime.InteropServices.Marshal]::PtrToStringBSTR($hopPointer)) }
        finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($hopPointer) }
        try {
            if ($hopPlain.Length -gt 16384) { throw 'The test key exceeds the encrypted envelope limit' }
            $hopEnvelope = [HopVmKeyEnvelope]::Envelope($hopPublic.public_key_pem, $hopPlain, $hopNonce)
        } finally { [Array]::Clear($hopPlain, 0, $hopPlain.Length) }
        $hopEnvelope | ConvertTo-Json -Compress |
            Set-Content -LiteralPath (Join-Path $hopJobs "$hopNonce-data.tmp") -Encoding utf8
        Move-Item -LiteralPath (Join-Path $hopJobs "$hopNonce-data.tmp") -Destination (Join-Path $hopJobs "$hopNonce-data.json")
    $hopJob | ConvertTo-Json -Compress
} finally {
    if (Test-Path -LiteralPath $hopMarker) { Remove-Item -LiteralPath $hopMarker }
}
