from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]
s=(root/'payload/vr-foveated/Build-And-Install-VRUniversalFoveated-v26.ps1').read_text(encoding='utf-8-sig')
helpers=s[s.index('static int VrCopyFormatFamily'):s.index('static bool VrFoveatedPackInputs')]
shader=re.search(r'    static const char kBlendSrc\[\] =.*?;\n\n    HMODULE',s,re.S).group(0).removesuffix('\n\n    HMODULE')
(root/'tests/vr-generated-test.h').write_text(helpers+'\n'+shader,encoding='utf-8')
