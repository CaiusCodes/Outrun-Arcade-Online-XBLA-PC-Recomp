param([string]$OutputDirectory)
$ErrorActionPreference='Stop'
if ($PSVersionTable.PSEdition -eq 'Core') {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath -OutputDirectory $OutputDirectory
    if($LASTEXITCODE -ne 0){throw 'Icon generation failed.'}
    return
}
if(-not $OutputDirectory){$OutputDirectory=Join-Path $PSScriptRoot '..\packaging\resources'}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
Add-Type -AssemblyName System.Drawing
if(-not ('OutrunIconBuilder' -as [type])) {
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Collections.Generic;
public static class OutrunIconBuilder {
    static Color C(int r,int g,int b){return Color.FromArgb(r,g,b);}
    static void Poly(Graphics g,Color c,params PointF[] p){using(var b=new SolidBrush(c))g.FillPolygon(b,p);}
    public static Bitmap Render(int size) {
        var bitmap=new Bitmap(size,size,PixelFormat.Format32bppArgb);
        using(var g=Graphics.FromImage(bitmap)) {
            g.SmoothingMode=SmoothingMode.AntiAlias; g.ScaleTransform(size/256f,size/256f);
            using(var shape=new GraphicsPath()) {
                shape.AddArc(4,4,40,40,180,90);shape.AddArc(212,4,40,40,270,90);shape.AddArc(212,212,40,40,0,90);shape.AddArc(4,212,40,40,90,90);shape.CloseFigure();
                using(var b=new LinearGradientBrush(new Rectangle(0,0,256,256),C(63,204,220),C(4,119,150),90))g.FillPath(b,shape);
            }
            using(var b=new SolidBrush(C(255,241,190)))g.FillEllipse(b,145,28,76,76);
            Poly(g,C(239,219,167),new PointF(0,231),new PointF(172,105),new PointF(234,112),new PointF(252,238),new PointF(230,251),new PointF(23,251));
            Poly(g,C(20,67,86),new PointF(54,252),new PointF(183,108),new PointF(209,108),new PointF(200,252));
            using(var p=new Pen(C(255,245,216),6)){g.DrawLine(p,193,120,178,142);g.DrawLine(p,159,169,139,199);g.DrawLine(p,121,224,105,247);}
            using(var p=new Pen(C(11,89,84),12))g.DrawBezier(p,45,184,65,151,45,103,70,64);
            Poly(g,C(9,81,84),new PointF(69,66),new PointF(19,44),new PointF(12,68),new PointF(69,75));
            Poly(g,C(8,106,89),new PointF(67,65),new PointF(104,35),new PointF(132,69),new PointF(75,74));
            Poly(g,C(12,98,83),new PointF(64,65),new PointF(52,23),new PointF(83,24),new PointF(75,64));
            using(var b=new SolidBrush(C(20,39,48))){g.FillRectangle(b,61,175,20,48);g.FillRectangle(b,187,175,20,48);}
            Poly(g,C(237,60,40),new PointF(65,208),new PointF(66,164),new PointF(91,129),new PointF(171,129),new PointF(202,164),new PointF(202,208));
            Poly(g,C(196,240,231),new PointF(84,162),new PointF(100,142),new PointF(163,142),new PointF(182,162));
            using(var b=new SolidBrush(C(134,39,43)))g.FillRectangle(b,72,172,124,9);
            using(var b=new SolidBrush(C(255,227,154))){g.FillRectangle(b,77,188,34,9);g.FillRectangle(b,159,188,32,9);}
            using(var b=new SolidBrush(C(245,246,220)))g.FillRectangle(b,118,196,29,10);
        }
        return bitmap;
    }
    public static void Write(string root) {
        int[] sizes={16,24,32,48,64,128,256};var images=new List<byte[]>();
        foreach(int size in sizes)using(var bitmap=Render(size))using(var stream=new MemoryStream()){bitmap.Save(stream,ImageFormat.Png);images.Add(stream.ToArray());if(size==256)bitmap.Save(Path.Combine(root,"outrun-icon.png"),ImageFormat.Png);}
        using(var output=new BinaryWriter(File.Create(Path.Combine(root,"outrun.ico")))){
            output.Write((ushort)0);output.Write((ushort)1);output.Write((ushort)sizes.Length);
            int offset=6+16*sizes.Length;
            for(int i=0;i<sizes.Length;i++){output.Write((byte)(sizes[i]==256?0:sizes[i]));output.Write((byte)(sizes[i]==256?0:sizes[i]));output.Write((byte)0);output.Write((byte)0);output.Write((ushort)1);output.Write((ushort)32);output.Write(images[i].Length);output.Write(offset);offset+=images[i].Length;}
            foreach(var bytes in images)output.Write(bytes);
        }
    }
}
"@
}
[OutrunIconBuilder]::Write([IO.Path]::GetFullPath($OutputDirectory))
