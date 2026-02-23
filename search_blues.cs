using System;
using System.Net.Http;
using System.Text.Json;
using System.Threading.Tasks;
using System.Linq;

class Program {
    static async Task Main() {
        var client = new HttpClient();
        client.DefaultRequestHeaders.Add("User-Agent", "Mozilla/5.0");
        var json = await client.GetStringAsync("https://dev.blues.io/page-data/search.json");
        using var doc = JsonDocument.Parse(json);
        foreach (var element in doc.RootElement.EnumerateArray()) {
            var str = element.ToString();
            if (str.Contains("Default Headers", StringComparison.OrdinalIgnoreCase)) {
                Console.WriteLine(str);
            }
        }
    }
}
