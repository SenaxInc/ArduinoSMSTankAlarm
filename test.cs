using System;
using System.Net.Http;
using System.Threading.Tasks;

class Program {
    static async Task Main() {
        var client = new HttpClient();
        client.DefaultRequestHeaders.Add("User-Agent", "Mozilla/5.0");
        var response = await client.GetStringAsync("https://dev.blues.io/notehub/notehub-walkthrough/");
        Console.WriteLine(response.Substring(0, 1000));
    }
}
