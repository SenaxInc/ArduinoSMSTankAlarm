
import urllib.request
import urllib.parse
from html.parser import HTMLParser

query = urllib.parse.quote('site:dev.blues.io Notehub route to Notehub API')
req = urllib.request.Request(f'https://html.duckduckgo.com/html/?q={query}', headers={'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64)'})

class MyParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self.in_a = False
        self.in_snippet = False
        self.results = []
        self.current_title = ''
        self.current_snippet = ''
    def handle_starttag(self, tag, attrs):
        if tag == 'a' and ('class', 'result__url') in attrs:
            self.in_a = True
        if tag == 'a' and ('class', 'result__snippet') in attrs:
            self.in_snippet = True
    def handle_endtag(self, tag):
        if tag == 'a':
            self.in_a = False
            self.in_snippet = False
            if self.current_title and self.current_snippet:
                self.results.append((self.current_title, self.current_snippet))
                self.current_title = ''
                self.current_snippet = ''
    def handle_data(self, data):
        if self.in_a:
            self.current_title += data
        if self.in_snippet:
            self.current_snippet += data

try:
    html = urllib.request.urlopen(req).read().decode('utf-8')
    parser = MyParser()
    parser.feed(html)
    for t, s in parser.results[:5]:
        print(f'URL: {t.strip()}\nSnippet: {s.strip()}\n')
except Exception as e:
    print(e)

