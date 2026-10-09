using System.Diagnostics;
using System.IO;
using System.Net.Http;
namespace AdobeKoreanShortcuts;
internal sealed class AutoUpdater : IDisposable
{
    readonly HttpClient http=new(){Timeout=TimeSpan.FromMinutes(5)};
    readonly CancellationTokenSource lifetime=new();
    CancellationTokenSource? attempt;
    internal static string Version=>typeof(Program).Assembly.GetName().Version!.ToString(3);
    internal string Status{get;private set;}="업데이트 대기";
    internal bool Busy{get;private set;}
    internal AutoUpdater(){http.DefaultRequestHeaders.UserAgent.ParseAdd("RIUM-Keys/"+Version);}
    internal void Cancel()=>attempt?.Cancel();
    internal async Task CheckAsync()
    {
        if(Busy)return;
        if(!Preferences.Installed){Status="설치 후 자동 업데이트 사용 가능";return;}
        Busy=true;using var cancel=CancellationTokenSource.CreateLinkedTokenSource(lifetime.Token);attempt=cancel;
        try
        {
            Status="업데이트 확인 중";
            using var response=await http.GetAsync(UpdateProtocol.Feed,HttpCompletionOption.ResponseHeadersRead,cancel.Token);
            response.EnsureSuccessStatusCode();
            using var manifest=new MemoryStream();
            await CopyBounded(await response.Content.ReadAsStreamAsync(cancel.Token),manifest,65536,cancel.Token);
            using var key=new StreamReader(typeof(Program).Assembly.GetManifestResourceStream("RiumKeys.UpdatePublicKey")!);
            var release=UpdateProtocol.Verify(System.Text.Encoding.UTF8.GetString(manifest.ToArray()),await key.ReadToEndAsync(cancel.Token));
            if(!UpdateProtocol.IsNewer(release,typeof(Program).Assembly.GetName().Version!)){Status="최신 버전 · "+Version;return;}
            Status="업데이트 다운로드 중";
            var dir=Path.Combine(Preferences.DataDir,"updates",release.Version);Directory.CreateDirectory(dir);
            var path=Path.Combine(dir,"RIUM-Keys-Setup.exe");
            using(var download=await http.GetAsync(release.Url,HttpCompletionOption.ResponseHeadersRead,cancel.Token))
            {
                download.EnsureSuccessStatusCode();
                using var file=new FileStream(path+".download",FileMode.Create,FileAccess.ReadWrite,FileShare.None);
                await CopyBounded(await download.Content.ReadAsStreamAsync(cancel.Token),file,release.Size,cancel.Token);
                file.Position=0;if(!UpdateProtocol.Matches(release,file))throw new InvalidDataException("Installer hash mismatch");
            }
            cancel.Token.ThrowIfCancellationRequested();File.Move(path+".download",path,true);
            var start=new ProcessStartInfo(path){UseShellExecute=false,CreateNoWindow=true};start.ArgumentList.Add("/S");start.ArgumentList.Add("/UPDATE");
            using var installer=Process.Start(start)??throw new IOException("Installer did not start");
            Status="업데이트 설치 중";
        }
        catch(OperationCanceledException){Status="업데이트 확인 취소됨";}
        catch(Exception ex){Status="업데이트 확인 실패 · 나중에 재시도";Preferences.Log(ex);}
        finally{attempt=null;Busy=false;}
    }
    static async Task CopyBounded(Stream input,Stream output,long limit,CancellationToken token)
    {
        using(input){byte[] buffer=new byte[65536];long total=0;int count;while((count=await input.ReadAsync(buffer,token))>0){total+=count;if(total>limit)throw new InvalidDataException("Download exceeds signed size");await output.WriteAsync(buffer.AsMemory(0,count),token);}}
    }
    public void Dispose(){lifetime.Cancel();http.Dispose();lifetime.Dispose();}
}

