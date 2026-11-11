using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Windows.Forms;

namespace Outrun.Setup {
    internal static class SetupPreview {
        // Render equivalent DPI geometry and font metrics without changing the
        // user's desktop. This is a layout simulation, not a physical 4K capture.
        internal static void Render(Form form,string path,int targetDpi) {
            var report=new List<string>();
            form.ShowInTaskbar=false;form.Opacity=0;form.Show();Application.DoEvents();
            float dpi;
            using(var g=form.CreateGraphics())dpi=g.DpiX;
            Size originalSize=form.ClientSize;
            report.Add("Actual desktop: "+Screen.PrimaryScreen.Bounds+"; graphics DPI: "+dpi);
            if(targetDpi>0) {
                if(targetDpi<96 || targetDpi>288)throw new ArgumentOutOfRangeException("targetDpi");
                // Client-only at 300%: a 1440p desktop clamps the extra title
                // bar even though this 1440px client fits a 2160px display.
                if(targetDpi==288){form.FormBorderStyle=FormBorderStyle.None;report.Add("300% preview: client only, desktop title-bar clamp excluded");}
                form.AutoScaleMode=AutoScaleMode.None;
                var fonts=new List<KeyValuePair<Control,Font>>();
                CollectFonts(form,fonts);
                foreach(var item in fonts)item.Key.Font=new Font(item.Value.FontFamily,item.Value.Size*targetDpi/dpi,item.Value.Style,item.Value.Unit);
                form.Scale(new SizeF(targetDpi/dpi,targetDpi/dpi));Application.DoEvents();
            }
            report.Add("Target DPI: "+(targetDpi>0?targetDpi:dpi)+"; client: "+form.ClientSize+"; window: "+form.Size);
            int issues=0;
            Check(form,report,ref issues);
            if(targetDpi>0 && (Math.Abs(form.ClientSize.Width-originalSize.Width*targetDpi/dpi)>4 || Math.Abs(form.ClientSize.Height-originalSize.Height*targetDpi/dpi)>4)) {
                report.Add("FAIL: client size does not match requested DPI");issues++;
            }
            using(var bitmap=new Bitmap(form.Width,form.Height)) {
                form.DrawToBitmap(bitmap,new Rectangle(Point.Empty,form.Size));
                bitmap.Save(path,System.Drawing.Imaging.ImageFormat.Png);
                if(targetDpi>0)using(var canvas=new Bitmap(3840,2160)) {
                    using(var g=Graphics.FromImage(canvas)) {
                        g.Clear(Color.FromArgb(29,48,62));
                        g.DrawImageUnscaled(bitmap,(3840-bitmap.Width)/2,(2160-bitmap.Height)/2);
                    }
                    canvas.Save(Path.ChangeExtension(path,"4k.png"),System.Drawing.Imaging.ImageFormat.Png);
                    if(bitmap.Width>3840 || bitmap.Height>2080){report.Add("FAIL: exceeds 4K work area");issues++;}
                }
            }
            report.Add(issues==0?"PASS: all visible controls/text fit; layout fits 4K canvas":"FAIL: "+issues+" layout issues");
            File.WriteAllLines(Path.ChangeExtension(path,"txt"),report.ToArray());
            if(issues>0)throw new InvalidOperationException("Preview layout validation failed: "+path);
        }
        static void CollectFonts(Control control,List<KeyValuePair<Control,Font>> fonts) {
            fonts.Add(new KeyValuePair<Control,Font>(control,control.Font));
            foreach(Control child in control.Controls)CollectFonts(child,fonts);
        }
        static void Check(Control parent,List<string> report,ref int issues) {
            foreach(Control child in parent.Controls) {
                if(!child.Visible)continue;
                if(child.Left<0 || child.Top<0 || child.Right>parent.ClientSize.Width+1 || child.Bottom>parent.ClientSize.Height+1) {
                    report.Add("FAIL: outside parent: "+child.GetType().Name+" "+child.Text+" "+child.Bounds);issues++;
                }
                if(child is Label || child is Button) {
                    var flags=TextFormatFlags.WordBreak;
                    Size measured;
                    using(var g=child.CreateGraphics())measured=TextRenderer.MeasureText(g,child.Text,child.Font,new Size(child.ClientSize.Width,10000),flags);
                    report.Add(child.GetType().Name+" | "+child.Text.Replace("\n"," / ")+" | "+child.Bounds+" font="+child.Font.Size+" measured="+measured);
                    if(measured.Height>child.ClientSize.Height+1 || measured.Width>child.ClientSize.Width+1) {
                        report.Add("FAIL: text exceeds control");issues++;
                    }
                }
                Check(child,report,ref issues);
            }
        }
    }
}
