using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Text;
using System.Threading.Tasks;

namespace MapTileGenerator.Core
{
    public class HttpTileLoadStrategy : ITileLoadStrategy
    {

        #region ITileLoadStrategy 成员

        public Stream GetTile(string url,int timeout)
        {
            //HttpWebRequest request = WebRequest.Create(url) as HttpWebRequest;
            //request.Timeout = 3000;
            //WebResponse response = request.GetResponse();
            //return response.GetResponseStream();
            if (timeout <= 10)
            {
                timeout = 10;
            }
            ServicePointManager.SecurityProtocol = SecurityProtocolType.Tls12;
            HttpClient httpClient = new HttpClient();
            httpClient.Timeout = TimeSpan.FromSeconds(timeout);
            httpClient.DefaultRequestHeaders.TryAddWithoutValidation("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
            return httpClient.GetStreamAsync(url).Result;
        }

        #endregion
    }
}
