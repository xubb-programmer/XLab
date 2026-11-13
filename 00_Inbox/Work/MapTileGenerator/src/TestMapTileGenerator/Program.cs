using MapTileGenerator;
using MapTileGenerator.Core;
using System;
using System.IO;
using System.Threading;

namespace TestMapTileGenerator
{
    class Program
    {
        private static TileGenerator generator = null;
        private static ManualResetEvent finishedEvent = new ManualResetEvent(false);

        static void Main(string[] args)
        {
            try
            {
                string configPath = args.Length > 0 ? args[0] : Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "mapConfig.json");
                if (!File.Exists(configPath))
                {
                    configPath = Path.Combine(Environment.CurrentDirectory, "mapConfig.json");
                }
                Console.WriteLine("加载配置: " + configPath);
                MapConfig mapConfig = MapConfig.Load(configPath);

                generator = new TileGenerator(mapConfig);
                generator.TileLoaded += (sender, tileCoord) =>
                {
                    if (generator.SuccessTile % 10 == 0 || generator.SuccessTile == generator.TotalTile)
                    {
                        Console.WriteLine(string.Format("[{0}/{1}] Zoom {2} ({3}, {4})",
                            generator.SuccessTile, generator.TotalTile, tileCoord.Zoom, tileCoord.X, tileCoord.Y));
                    }
                };
                generator.Finished += (sender, msg) =>
                {
                    Console.WriteLine("=== 下载完成 ===");
                    Console.WriteLine(msg);
                    finishedEvent.Set();
                };

                Console.WriteLine("开始下载瓦片... 计划瓦片数: " + generator.TotalTile);
                generator.Start();

                finishedEvent.WaitOne();
                generator.Close();
                Console.WriteLine("执行完毕，正常退出。");
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine("执行异常: " + ex);
                Environment.ExitCode = 1;
            }
        }
    }
}
