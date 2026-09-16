import os
K = os.path.dirname(__file__)
tpl = open(K + '/template.html').read()
data = open(K + '/sun_data.js').read()
page = tpl.replace('/*__DATA__*/', data).replace('/*__MODEL__*/', open(K + '/sun_model.js').read())
os.makedirs(K + '/out', exist_ok=True)
open(K + '/out/sun-simulator-artifact.html', 'w').write(page)          # artifact: skeleton added at publish
standalone = ('<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">'
              + page.replace('<canvas id="c"', '</head><body><canvas id="c"', 1) + '</body></html>')
open(K + '/out/sun-simulator.html', 'w').write(standalone)
print(len(page), len(standalone))
