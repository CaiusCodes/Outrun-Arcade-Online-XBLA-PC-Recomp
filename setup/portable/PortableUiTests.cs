using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;
namespace Outrun.Setup {
 internal static class PortableUiTests {
  static void Check(bool ok,string message){if(!ok){Console.Error.WriteLine("FAIL: "+message);throw new Exception(message);}}
  static System.Collections.Generic.IEnumerable<Control> All(Control root){foreach(Control child in root.Controls){yield return child;foreach(var nested in All(child))yield return nested;}}
  static Task Call(ImportForm form,string name,params object[] args){return (Task)typeof(ImportForm).GetMethod(name,BindingFlags.Instance|BindingFlags.NonPublic).Invoke(form,args);}
  static void Finish(Task task){var until=DateTime.UtcNow.AddSeconds(30);while(!task.IsCompleted&&DateTime.UtcNow<until){Application.DoEvents();Thread.Sleep(10);}Check(task.IsCompleted,"UI operation timed out");task.GetAwaiter().GetResult();Application.DoEvents();}
  [STAThread]static int Main(string[] args){try{
   Application.EnableVisualStyles();var fresh=new PortableGame(args[0]);
   using(var form=new ImportForm(fresh,null,true)){
    form.RefreshReady();Check(!form.PlayEnabled&&form.BrowseEnabled,"Fresh gating");
    Check(All(form).OfType<CheckBox>().Count()==2&&All(form).OfType<CheckBox>().All(c=>!c.Checked),"Unchecked Extras");
    Check(!All(form).OfType<Label>().Any(c=>c.Text.Contains("No compatible")),"Redundant status");
    Check(form.ClientSize.Width==960&&form.ClientSize.Height==630,"Coastal layout");
    Check(All(form).All(c=>c.AllowDrop),"Drop targets missing on nested controls");
    form.ShowInTaskbar=false;form.Opacity=0;try{form.Show();Application.DoEvents();}catch(Exception e){Console.Error.WriteLine("Opening test form: "+e);throw;}
    SynchronizationContext.SetSynchronizationContext(new WindowsFormsSynchronizationContext());
    Directory.CreateDirectory(args[0]);string invalid=Path.Combine(args[0],"invalid-package.txt");File.WriteAllText(invalid,"Not an Xbox package");
    Finish(Call(form,"SelectInput",invalid));
    Check(!form.PlayEnabled&&form.BrowseEnabled&&!Directory.Exists(fresh.Assets),"Invalid input accepted or gating failed");
    Check(All(form).OfType<LinkLabel>().Any(c=>c.Visible),"Error detail link missing");
    Check(File.ReadAllText(invalid)=="Not an Xbox package","Original input changed");
    var task=Call(form,"Run",(Action)(()=>{var token=((CancellationTokenSource)typeof(ImportForm).GetField("cancellation",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(form)).Token;token.WaitHandle.WaitOne(5000);token.ThrowIfCancellationRequested();}),"Checking cancellation");
    Check(!form.PlayEnabled&&!form.BrowseEnabled,"Busy gating failed");
    Check(All(form).OfType<CheckBox>().All(c=>!c.Enabled),"Busy Extras still enabled");
    All(form).OfType<Button>().Single(b=>b.Text=="Cancel").PerformClick();Finish(task);
    Check(form.BrowseEnabled&&!form.PlayEnabled,"Cancel did not restore gating");
    Check(All(form).OfType<Label>().Any(c=>c.Text=="Import cancelled"),"Cancellation status missing");
    Check(!All(form).OfType<LinkLabel>().Any(c=>c.Visible),"Stale error after cancellation");
    form.Close();
   }
   var imported=new PortableGame(args[1]);Check(imported.Ready(),"Existing imported fixture not ready");
   using(var form=new ImportForm(imported,null,true)){form.RefreshReady();Check(form.PlayEnabled&&!form.BrowseEnabled,"Imported gating");}
   foreach(string state in new[]{"empty","selected","validation","progress","complete","error"})using(var form=new ImportForm(fresh,null,true)){
    form.Preview(state);Check(form.PlayEnabled==(state=="complete"),"Preview Play gating: "+state);
    Check(form.BrowseEnabled!=(state=="validation"||state=="progress"||state=="complete"),"Preview Browse gating: "+state);
   }
   Console.WriteLine("PASS: clean/imported Play gating, unchecked Extras, nested drop targets, actual invalid-package rejection and preservation, async Cancel and busy gating, recoverable error details, six preview states; imported fixture read only");return 0;
  }catch(Exception e){Console.Error.WriteLine(e);return 1;}}
 }
}
