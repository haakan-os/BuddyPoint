#!/usr/bin/env python3
"""Native editor integration test. Supply LuaJIT and an optional KOReader bootstrap Lua file.
Example: python3 scripts/test_epub_native.py --lua /path/to/luajit --bootstrap /path/to/bootstrap.lua
The bootstrap must expose ffi/archiver and libs/libkoreader-lfs. Image rendering is stubbed.
"""
import argparse
import io
import os
from pathlib import Path
import stat
import subprocess
import tempfile
import warnings
import zipfile
import xml.etree.ElementTree as ET
from PIL import Image

parser=argparse.ArgumentParser()
parser.add_argument('--lua',default='luajit')
parser.add_argument('--bootstrap')
args=parser.parse_args()
script=Path(__file__).resolve().with_suffix('.lua')
with tempfile.TemporaryDirectory(prefix='haakanpoint-native-') as directory:
    root=Path(directory)
    image=Image.frombytes('RGB',(1600,600),os.urandom(1600*600*3))
    jpeg=io.BytesIO();image.save(jpeg,format='JPEG',quality=90)
    replacement=Image.new('L',(480,180),128);replacement.save(root/'replacement.png')
    data={
      'mimetype':b'application/epub+zip',
      'META-INF/container.xml':b'<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>',
      'OPS/book.opf':b'''<opf:package xmlns:opf="http://www.idpf.org/2007/opf" version="3.0"><opf:manifest>
        <opf:item id='chapter' href='Text/chapter.xhtml' media-type='application/xhtml+xml'/>
        <opf:item id='a' media-type='image/jpeg' href='a/cover.jpg'></opf:item>
        <opf:item id='b' href='b/cover.jpg' media-type='image/jpeg'/>
        <opf:item id='collision' href='a/cover.png' media-type='image/png'/>
        <opf:item id='font' href='Fonts/Body%20%26%20Serif.ttf' media-type='font/ttf'></opf:item>
        <opf:item id='css' href='Styles/main.css' media-type='text/css'/>
        </opf:manifest><opf:spine><opf:itemref idref='chapter'/></opf:spine></opf:package>''',
      'OPS/Text/chapter.xhtml':b'''<html xmlns="http://www.w3.org/1999/xhtml" xmlns:xlink="http://www.w3.org/1999/xlink"><head><style><![CDATA[.cover {background:url('../a/cover.jpg')}]]></style></head><body>
        <!-- keep cover.jpg and fake <img src="../a/cover.jpg"/> unchanged -->
        <p>cover.jpg stays literal; &amp; stays valid</p>
        <img src="../a/cover.jpg#page"/><img src='../b/cover.jpg'/>
        <img srcset="../a/cover.jpg 1x, ../b/cover.jpg 2x"/>
        <svg><image xlink:href="../a/cover.jpg"/></svg>
        <div style="background:url(&apos;../b/cover.jpg&apos;)"/>
        <a href="https://example.com/cover.jpg">remote</a>
        </body></html>''',
      'OPS/Styles/main.css':b'''/* url('../a/cover.jpg') is just a comment */
        @font-face { font-family: "Body { serif }"; src: url('../Fonts/Body%20%26%20Serif.ttf'); }
        .a {background:url(../a/cover.jpg)} .b {background:URL("../b/cover.jpg")}
        .literal:after {content:"cover.jpg"}''',
      'META-INF/encryption.xml':b'''<encryption xmlns:e="http://www.w3.org/2001/04/xmlenc#"><e:EncryptedData><e:CipherData><e:CipherReference URI="OPS/Fonts/Body%20%26%20Serif.ttf"/></e:CipherData></e:EncryptedData></encryption>''',
      'OPS/Fonts/Body & Serif.ttf':b'font'*1024,
      'OPS/a/cover.jpg':jpeg.getvalue(), 'OPS/b/cover.jpg':jpeg.getvalue(),
      'OPS/a/cover.png':(root/'replacement.png').read_bytes(),
      'empty.txt':b'',
    }
    def archive(name,entries):
        with zipfile.ZipFile(root/name,'w',zipfile.ZIP_DEFLATED) as z:
            for key,value in entries.items():z.writestr(key,value)
    archive('book.epub',data)
    archive('traversal.epub',dict(data,**{'../outside.txt':b'escape'}))
    archive('missing.epub',{k:v for k,v in data.items() if k!='OPS/Text/chapter.xhtml'})
    archive('badspine.epub',dict(data,**{'OPS/book.opf':data['OPS/book.opf'].replace(b"idref='chapter'",b"idref='absent'")}))
    (root/'broken.epub').write_bytes(b'not a zip')
    archive('duplicate.epub',data)
    with warnings.catch_warnings():
        warnings.simplefilter('ignore')
        with zipfile.ZipFile(root/'duplicate.epub','a') as z:z.writestr('mimetype',b'bad')
    archive('symlink.epub',data)
    with zipfile.ZipFile(root/'symlink.epub','a') as z:
        info=zipfile.ZipInfo('escape');info.create_system=3;info.external_attr=(stat.S_IFLNK|0o777)<<16
        z.writestr(info,b'../outside.txt')
    cmd=[args.lua]
    if args.bootstrap:
        # Pass the path via an environment variable, without interpolating it into Lua source.
        os.environ['HAAKANPOINT_TEST_BOOTSTRAP']=str(Path(args.bootstrap).resolve())
        cmd+=['-e',"dofile(os.getenv('HAAKANPOINT_TEST_BOOTSTRAP'))"]
    subprocess.run(cmd+[str(script),str(root)],check=True)
    with zipfile.ZipFile(root/'result.epub') as z:
        assert z.testzip() is None
        assert z.namelist()[0]=='mimetype' and z.getinfo('mimetype').compress_type==zipfile.ZIP_STORED
        for info in z.infolist():
            if info.filename.lower().endswith(('.png', '.jpg', '.jpeg', '.gif', '.webp')):
                assert info.compress_type == zipfile.ZIP_STORED, info.filename
            elif info.filename.endswith(('.xhtml', '.opf', '.css')):
                assert info.compress_type == zipfile.ZIP_DEFLATED, info.filename
        assert 'OPS/Fonts/Body & Serif.ttf' not in z.namelist()
        assert 'OPS/a/cover.jpg' not in z.namelist() and 'OPS/b/cover.jpg' not in z.namelist()
        assert z.read('OPS/a/cover.png')==data['OPS/a/cover.png']
        assert z.read('OPS/a/cover-x3-1.png')==(root/'replacement.png').read_bytes()
        assert z.read('OPS/b/cover.png')==(root/'replacement.png').read_bytes()
        opf=ET.fromstring(z.read('OPS/book.opf'))
        items={i.attrib['id']:i.attrib for i in opf.iter() if i.tag.endswith('}item')}
        assert 'font' not in items
        assert items['a']['href']=='a/cover-x3-1.png' and items['a']['media-type']=='image/png'
        assert items['b']['href']=='b/cover.png' and items['b']['media-type']=='image/png'
        chapter=z.read('OPS/Text/chapter.xhtml').decode();ET.fromstring(chapter)
        assert 'src="../a/cover-x3-1.png#page"' in chapter
        assert "src='../b/cover.png'" in chapter
        assert 'srcset="../a/cover-x3-1.png 1x, ../b/cover.png 2x"' in chapter
        assert 'cover.jpg stays literal' in chapter and 'https://example.com/cover.jpg' in chapter
        assert 'fake <img src="../a/cover.jpg"/>' in chapter
        css=z.read('OPS/Styles/main.css').decode()
        assert '@font-face' not in css and '../a/cover-x3-1.png' in css and '../b/cover.png' in css
        assert "/* url('../a/cover.jpg') is just a comment */" in css
        encryption=ET.fromstring(z.read('META-INF/encryption.xml'))
        assert not any(e.tag.endswith('EncryptedData') for e in encryption.iter())
        assert z.read('empty.txt')==b''
    print('PASS: independent ZIP/XML verification: intact contents, distinct images, valid manifest/spine, font cleanup, references and CRCs')
    # Unmodified images must also use the low-memory extraction path.
    with zipfile.ZipFile(root/'editor-copy.epub') as z:
        for name in ('OPS/a/cover.jpg', 'OPS/b/cover.jpg', 'OPS/a/cover.png'):
            assert z.getinfo(name).compress_type == zipfile.ZIP_STORED
            assert z.read(name) == data[name]
    print('PASS: compressed image formats are stored without a second compression layer; text remains compressed')
