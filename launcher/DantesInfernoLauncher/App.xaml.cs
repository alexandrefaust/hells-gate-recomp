using System;
using System.IO;
using System.Windows;
using System.Windows.Threading;

namespace DantesInferno.Launcher
{
    public partial class App : Application
    {
        protected override void OnStartup(StartupEventArgs e)
        {
            DispatcherUnhandledException += OnDispatcherUnhandledException;
            base.OnStartup(e);
        }

        // Shows the error instead of closing silently, and keeps it in
        // %LOCALAPPDATA%\DantesInferno\launcher_error.log.
        private void OnDispatcherUnhandledException(object sender, DispatcherUnhandledExceptionEventArgs e)
        {
            try
            {
                string dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "DantesInferno");
                Directory.CreateDirectory(dir);
                File.AppendAllText(Path.Combine(dir, "launcher_error.log"),
                    DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss") + " " + e.Exception + Environment.NewLine + Environment.NewLine);
            }
            catch { }
            MessageBox.Show(e.Exception.Message, "Dante's Inferno Launcher", MessageBoxButton.OK, MessageBoxImage.Error);
            e.Handled = true;
        }
    }
}
