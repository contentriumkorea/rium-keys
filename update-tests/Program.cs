using AdobeKoreanShortcuts;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text;
using var rsa=RSA.Create(2048);
var pub=rsa.ExportSubjectPublicKeyInfoPem();
byte[] file=Encoding.UTF8.GetBytes("installer bytes");
var release=new UpdateRelease("1.0.1","https://github.com/contentriumkorea/rium-keys/releases/download/v1.0.1/RIUM-Keys-Setup.exe",Convert.ToHexString(SHA256.HashData(file)),file.Length);
string Sign(UpdateRelease r){var b=JsonSerializer.SerializeToUtf8Bytes(r);return JsonSerializer.Serialize(new UpdateEnvelope(Convert.ToBase64String(b),Convert.ToBase64String(rsa.SignData(b,HashAlgorithmName.SHA256,RSASignaturePadding.Pkcs1))));}
int count=0;
void Check(bool value,string name){if(!value)throw new Exception(name);Console.WriteLine("PASS "+name);count++;}
void Reject(Action a,string name){try{a();}catch{Check(true,name);return;}throw new Exception("Accepted "+name);}
var good=Sign(release);
Check(UpdateProtocol.Verify(good,pub)==release,"valid signed manifest");
using var other=RSA.Create(2048);
Reject(()=>UpdateProtocol.Verify(good,other.ExportSubjectPublicKeyInfoPem()),"wrong publisher key");
var env=JsonSerializer.Deserialize<UpdateEnvelope>(good)!;
Reject(()=>UpdateProtocol.Verify(JsonSerializer.Serialize(env with {Payload=Convert.ToBase64String(Encoding.UTF8.GetBytes("tampered"))}),pub),"tampered manifest");
Reject(()=>UpdateProtocol.Verify(Sign(release with{Url="https://evil.example/setup.exe"}),pub),"foreign installer URL");
Reject(()=>UpdateProtocol.Verify(Sign(release with{Url=release.Url.Replace("https:","http:")}),pub),"HTTP URL");
Reject(()=>UpdateProtocol.Verify(Sign(release with{Url=release.Url+"?x=1"}),pub),"query URL");
Reject(()=>UpdateProtocol.Verify(Sign(release with{Size=300L*1024*1024}),pub),"oversized installer");
Reject(()=>UpdateProtocol.Verify(Sign(release with{Sha256="invalid"}),pub),"invalid hash");
Reject(()=>UpdateProtocol.Verify(Sign(release with{Version="oops"}),pub),"invalid version");
Check(UpdateProtocol.IsNewer(release,new Version(1,0,0,0)),"new version");
Check(!UpdateProtocol.IsNewer(release,new Version(1,0,1,0)),"same version");
Check(!UpdateProtocol.IsNewer(release,new Version(2,0,0,0)),"reject downgrade");
Check(UpdateProtocol.Matches(release,new MemoryStream(file)),"installer integrity");
file[0]^=1;
Check(!UpdateProtocol.Matches(release,new MemoryStream(file)),"installer tamper");
Check(!UpdateProtocol.Matches(release,new MemoryStream(new byte[1])),"installer length");
Console.WriteLine($"{count} checks passed");
