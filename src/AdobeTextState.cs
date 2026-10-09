using System.Diagnostics;
using System.Windows.Automation;
namespace AdobeKoreanShortcuts;
internal sealed class AdobeTextState
{
    uint pid;
    AutomationElementCollection? tools;
    internal bool ProtectCanvas(uint processId,string app,string context)
    {
        bool ae=app=="AfterFX" && context.Contains("AE Composition/");
        bool premiere=app=="Adobe Premiere Pro" && context.Contains("Program Monitor/");
        if(!ae && !premiere)return false;
        try
        {
            if(pid!=processId || tools==null)
            {
                using var process=Process.GetProcessById((int)processId);
                var root=AutomationElement.FromHandle(process.MainWindowHandle);
                AutomationElement? toolbar=null;
                if(ae)
                    toolbar=root.FindFirst(TreeScope.Children,new OrCondition(
                        new PropertyCondition(AutomationElement.NameProperty,"ToolsTab"),
                        new PropertyCondition(AutomationElement.ClassNameProperty,"ToolsTab")));
                else
                {
                    // Search only the three native workspace levels, never embedded CEP documents.
                    foreach(AutomationElement frame in root.FindAll(TreeScope.Children,Condition.TrueCondition))
                    {
                        if(frame.Current.Name!="WorkspaceFrame")continue;
                        foreach(AutomationElement panel in frame.FindAll(TreeScope.Children,Condition.TrueCondition))
                        {
                            toolbar=panel.FindFirst(TreeScope.Children,new PropertyCondition(AutomationElement.NameProperty,"Tools"));
                            if(toolbar!=null)break;
                        }
                        if(toolbar!=null)break;
                    }
                }
                if(toolbar==null)return true;
                tools=toolbar.FindAll(TreeScope.Descendants,new OrCondition(
                    new PropertyCondition(AutomationElement.NameProperty,"Horizontal Type Tool (Ctrl+T)"),
                    new PropertyCondition(AutomationElement.NameProperty,"Vertical Type Tool (Ctrl+T)"),
                    new PropertyCondition(AutomationElement.NameProperty,"Type Tool (T)"),
                    new PropertyCondition(AutomationElement.NameProperty,"Vertical Type Tool (T)")));
                pid=processId;
            }
            if(tools.Count==0)return true;
            foreach(AutomationElement tool in tools)
            {
                if(!tool.TryGetCurrentPattern(ValuePattern.Pattern,out var pattern))return true;
                var value=((ValuePattern)pattern).Current.Value;
                if(value!="Not Selected")return true;
            }
            return false;
        }
        catch {tools=null;return true;}
    }
}
