using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

namespace Outrun.Setup {
    internal static class CoastPalette {
        internal static readonly Color Ink=Color.FromArgb(12,48,63),Muted=Color.FromArgb(83,111,120),Paper=Color.FromArgb(246,247,237),Teal=Color.FromArgb(0,126,139),Red=Color.FromArgb(220,61,43),Gold=Color.FromArgb(255,190,51);
    }
    // Original vector scenery, independent of imported game assets. Logical
    // coordinates scale with the control, rather than stretching a bitmap.
    internal sealed class CoastScene:Control {
        internal CoastScene(){DoubleBuffered=true;AccessibleRole=AccessibleRole.Graphic;AccessibleName="OutRun Online Arcade coastal highway illustration";}
        static void Poly(Graphics g,Color c,params PointF[] points){using(var b=new SolidBrush(c))g.FillPolygon(b,points);}
        static void Rect(Graphics g,Color c,float x,float y,float w,float h){using(var b=new SolidBrush(c))g.FillRectangle(b,x,y,w,h);}
        static void Palm(Graphics g,float x,float y,float scale){
            var save=g.Save();g.TranslateTransform(x,y);g.ScaleTransform(scale,scale);
            using(var p=new Pen(Color.FromArgb(24,92,87),5)){g.DrawBezier(p,0,0,2,-30,16,-64,11,-91);}
            for(int i=0;i<7;i++){
                double angle=(-170+i*27)*Math.PI/180;float dx=(float)Math.Cos(angle)*53,dy=(float)Math.Sin(angle)*27;
                using(var p=new GraphicsPath()){
                    p.AddBezier(11,-91,11+dx*.35f,-104+dy,11+dx,-102+dy,11+dx,-78+dy);
                    p.AddBezier(11+dx,-78+dy,11+dx*.60f,-100+dy,17,-92,11,-91);
                    using(var b=new SolidBrush(i%2==0?Color.FromArgb(15,107,91):Color.FromArgb(20,132,102)))g.FillPath(b,p);
                }
            }g.Restore(save);
        }
        protected override void OnPaint(PaintEventArgs e){
            var g=e.Graphics;g.SmoothingMode=SmoothingMode.AntiAlias;g.ScaleTransform(Width/960f,Height/236f);
            g.Clear(Color.FromArgb(66,194,218));
            using(var b=new SolidBrush(Color.FromArgb(255,223,117)))g.FillEllipse(b,765,20,105,105);
            using(var b=new SolidBrush(Color.FromArgb(231,249,241))){g.FillEllipse(b,477,55,64,18);g.FillEllipse(b,490,41,39,29);g.FillEllipse(b,520,60,53,13);g.FillEllipse(b,619,34,54,12);}
            Poly(g,Color.FromArgb(55,161,180),new PointF(490,136),new PointF(567,107),new PointF(622,131),new PointF(685,99),new PointF(736,135),new PointF(960,119),new PointF(960,179),new PointF(490,179));
            Rect(g,Color.FromArgb(14,156,183),0,143,960,93);Rect(g,Color.FromArgb(115,222,224),0,143,960,4);
            using(var pen=new Pen(Color.FromArgb(91,204,211),1.5f)){g.DrawLine(pen,42,183,286,183);g.DrawLine(pen,254,216,489,216);g.DrawLine(pen,478,164,577,164);}
            using(var beach=new GraphicsPath()){
                beach.AddBezier(752,141,607,174,793,162,591,236);beach.AddLine(591,236,960,236);beach.AddLine(960,236,960,144);beach.CloseFigure();using(var b=new SolidBrush(Color.FromArgb(245,224,176)))g.FillPath(b,beach);
            }
            using(var road=new GraphicsPath()){
                road.AddBezier(766,142,672,171,795,173,659,236);road.AddLine(659,236,869,236);road.AddBezier(869,236,727,180,772,166,778,142);road.CloseFigure();
                using(var b=new SolidBrush(Color.FromArgb(55,82,88)))g.FillPath(b,road);using(var p=new Pen(Color.FromArgb(255,249,220),3))g.DrawPath(p,road);
            }
            using(var p=new Pen(Color.FromArgb(255,241,186),2)){p.DashPattern=new float[]{5,6};g.DrawBezier(p,772,144,703,176,790,175,756,236);}
            Palm(g,902,214,1.42f);Palm(g,828,160,.55f);Palm(g,706,161,.36f);
            // A small original roadster silhouette; no manufacturer marks.
            var car=g.Save();g.TranslateTransform(756,204);g.RotateTransform(-6);
            Rect(g,Color.FromArgb(21,42,48),-26,-1,9,18);Rect(g,Color.FromArgb(21,42,48),19,-1,9,18);
            Poly(g,CoastPalette.Red,new PointF(-27,13),new PointF(-24,-9),new PointF(-15,-20),new PointF(16,-20),new PointF(26,-8),new PointF(28,13));
            Poly(g,Color.FromArgb(158,219,219),new PointF(-16,-8),new PointF(-10,-16),new PointF(11,-16),new PointF(18,-8));
            Rect(g,Color.FromArgb(13,48,58),-22,0,45,4);Rect(g,Color.FromArgb(255,222,132),-22,7,10,3);Rect(g,Color.FromArgb(255,222,132),14,7,10,3);g.Restore(car);
            // Typographic wordmark, drawn from a system font, not copied logo art.
            using(var path=new GraphicsPath())using(var family=new FontFamily("Arial")){
                path.AddString("OutRun",family,(int)(FontStyle.Bold|FontStyle.Italic),82,new PointF(30,23),StringFormat.GenericDefault);
                using(var outline=new Pen(Color.FromArgb(242,255,247),5)){outline.LineJoin=LineJoin.Round;g.DrawPath(outline,path);}using(var b=new SolidBrush(CoastPalette.Ink))g.FillPath(b,path);
            }
            using(var f=new Font("Segoe UI",15,FontStyle.Bold,GraphicsUnit.Pixel))using(var b=new SolidBrush(CoastPalette.Ink))g.DrawString("O N L I N E   A R C A D E",f,b,38,116);
            Rect(g,CoastPalette.Red,38,154,38,5);Rect(g,CoastPalette.Gold,76,154,38,5);Rect(g,Color.FromArgb(249,249,229),114,154,212,5);
            Rect(g,CoastPalette.Red,0,232,320,4);Rect(g,CoastPalette.Gold,320,232,320,4);Rect(g,CoastPalette.Paper,640,232,320,4);
        }
    }
    internal sealed class ArcadeButton:Button {
        internal bool Primary;
        bool hover,pressed;
        internal ArcadeButton(){FlatStyle=FlatStyle.Flat;FlatAppearance.BorderSize=0;DoubleBuffered=true;Cursor=Cursors.Hand;UseVisualStyleBackColor=false;}
        protected override void OnMouseEnter(EventArgs e){hover=true;Invalidate();base.OnMouseEnter(e);}
        protected override void OnMouseLeave(EventArgs e){hover=pressed=false;Invalidate();base.OnMouseLeave(e);}
        protected override void OnMouseDown(MouseEventArgs e){pressed=true;Invalidate();base.OnMouseDown(e);}
        protected override void OnMouseUp(MouseEventArgs e){pressed=false;Invalidate();base.OnMouseUp(e);}
        protected override void OnEnabledChanged(EventArgs e){Cursor=Enabled?Cursors.Hand:Cursors.Default;Invalidate();base.OnEnabledChanged(e);}
        protected override void OnPaint(PaintEventArgs e){
            var g=e.Graphics;g.SmoothingMode=SmoothingMode.AntiAlias;g.Clear(BackColor);
            var bg=!Enabled?Color.FromArgb(206,218,216):Primary?(pressed?Color.FromArgb(190,49,35):hover?Color.FromArgb(239,79,54):CoastPalette.Red):(pressed?Color.FromArgb(0,93,108):hover?Color.FromArgb(0,147,160):CoastPalette.Teal);
            float cut=Height*.23f;
            using(var path=new GraphicsPath()){
                path.AddPolygon(new PointF[]{new PointF(cut,0),new PointF(Width-1,0),new PointF(Width-1-cut,Height-1),new PointF(0,Height-1)});
                using(var b=new SolidBrush(bg))g.FillPath(b,path);
                if(Focused&&ShowFocusCues)using(var p=new Pen(Enabled?Color.White:CoastPalette.Muted,2)){p.DashStyle=DashStyle.Dot;g.DrawPath(p,path);}
            }
            TextRenderer.DrawText(g,Text,Font,ClientRectangle,Enabled?Color.White:Color.FromArgb(101,125,128),TextFormatFlags.HorizontalCenter|TextFormatFlags.VerticalCenter|TextFormatFlags.SingleLine);
        }
    }
    internal sealed class PackagePanel:Panel {
        internal Color Accent=Color.FromArgb(182,204,202);
        internal bool DragActive,HasValidPackage;
        internal PackagePanel(){DoubleBuffered=true;BackColor=Color.White;}
        protected override void OnPaint(PaintEventArgs e){base.OnPaint(e);using(var p=new Pen(DragActive?CoastPalette.Teal:Accent,DragActive?3:1)){p.DashStyle=DragActive||HasValidPackage?DashStyle.Solid:DashStyle.Dash;e.Graphics.DrawRectangle(p,1,1,Width-3,Height-3);}}
    }
    internal sealed class ImportProgress:Control {
        readonly Timer timer=new Timer{Interval=40};int value,phase;bool indeterminate;
        internal int Value{get{return value;}set{this.value=Math.Max(0,Math.Min(100,value));Invalidate();}}
        internal bool Indeterminate{get{return indeterminate;}set{indeterminate=value;timer.Enabled=value&&Visible;Invalidate();}}
        internal ImportProgress(){DoubleBuffered=true;timer.Tick+=(s,e)=>{phase=(phase+2)%140;Invalidate();};}
        protected override void OnVisibleChanged(EventArgs e){timer.Enabled=indeterminate&&Visible;base.OnVisibleChanged(e);}
        protected override void Dispose(bool disposing){if(disposing)timer.Dispose();base.Dispose(disposing);}
        protected override void OnPaint(PaintEventArgs e){e.Graphics.Clear(Color.FromArgb(47,81,91));using(var b=new SolidBrush(CoastPalette.Gold)){if(indeterminate)e.Graphics.FillRectangle(b,Width*(phase-40)/100,0,Width*40/100,Height);else e.Graphics.FillRectangle(b,0,0,Width*value/100,Height);}}
    }
}
