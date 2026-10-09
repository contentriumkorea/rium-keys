using System.IO;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.RegularExpressions;
namespace AdobeKoreanShortcuts;
public sealed record UpdateEnvelope(string Payload,string Signature);
public sealed record UpdateRelease(string Version,string Url,string Sha256,long Size);
public static class UpdateProtocol
{
    public const string Feed="https://github.com/contentriumkorea/rium-keys/releases/latest/download/update.json";
    public static UpdateRelease Verify(string json,string publicKey)
    {
        if(json.Length>65536)throw new InvalidDataException("Manifest too large");
        var envelope=JsonSerializer.Deserialize<UpdateEnvelope>(json)??throw new InvalidDataException("Empty manifest");
        byte[] bytes=Convert.FromBase64String(envelope.Payload),signature=Convert.FromBase64String(envelope.Signature);
        using var rsa=RSA.Create();rsa.ImportFromPem(publicKey);
        if(!rsa.VerifyData(bytes,signature,HashAlgorithmName.SHA256,RSASignaturePadding.Pkcs1))throw new CryptographicException("Invalid update signature");
        var release=JsonSerializer.Deserialize<UpdateRelease>(bytes)??throw new InvalidDataException("Empty release");
        if(!System.Version.TryParse(release.Version,out var version)||version.Build<0||version.Revision>0)throw new InvalidDataException("Invalid version");
        if(!Uri.TryCreate(release.Url,UriKind.Absolute,out var url)||url.Scheme!="https"||url.Host!="github.com"||!url.IsDefaultPort||url.UserInfo!=""||url.Query!=""||url.Fragment!=""||
            url.AbsolutePath!=$"/contentriumkorea/rium-keys/releases/download/v{release.Version}/RIUM-Keys-Setup.exe")throw new InvalidDataException("Invalid installer URL");
        if(release.Sha256==null||!Regex.IsMatch(release.Sha256,"\\A[0-9a-fA-F]{64}\\z")||release.Size<=0||release.Size>256L*1024*1024)throw new InvalidDataException("Invalid installer metadata");
        return release;
    }
    public static bool IsNewer(UpdateRelease release,Version current)=>System.Version.Parse(release.Version)>current;
    public static bool Matches(UpdateRelease release,Stream stream)
    {return stream.Length==release.Size && string.Equals(Convert.ToHexString(SHA256.HashData(stream)),release.Sha256,StringComparison.OrdinalIgnoreCase);}
}
