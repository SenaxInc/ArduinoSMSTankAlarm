import urllib.request
import json

url = 'https://dev.blues.io/page-data/search.json'
try:
    req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
    data = json.loads(urllib.request.urlopen(req).read().decode('utf-8'))
    print('Search data loaded')
except Exception as e:
    print(e)
