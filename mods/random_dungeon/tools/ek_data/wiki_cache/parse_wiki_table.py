import re,csv,sys,html as H
src,dst=sys.argv[1],sys.argv[2]
t=open(src,encoding='utf8').read()
rows=re.findall(r'<tr[^>]*>(.*?)</tr>',t,re.S)
w=csv.writer(open(dst,'w'))
def orig(u):
    m=re.match(r'/wiki/images/thumb/(\w/\w\w/[^/]+)/',u)
    return '/wiki/images/'+m.group(1) if m else u
for r in rows:
    cells=re.findall(r'<t[dh][^>]*>(.*?)</t[dh]>',r,re.S)
    if not cells: continue
    imgs=re.findall(r'src="([^"]+)"',cells[0])
    out=[]
    for c in cells[1:]:
        c=re.sub(r'<img alt="([^".]+)\.png"[^>]*>',lambda m:'['+m.group(1)+']',c)
        out.append(H.unescape(re.sub(r'<[^>]+>','',c)).strip().replace('\n',' '))
    w.writerow(out+[orig(imgs[0]) if imgs else ''])
