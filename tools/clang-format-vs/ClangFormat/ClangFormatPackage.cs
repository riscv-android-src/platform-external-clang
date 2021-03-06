//===-- ClangFormatPackages.cs - VSPackage for clang-format ------*- C# -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This class contains a VS extension package that runs clang-format over a
// selection in a VS text editor.
//
//===----------------------------------------------------------------------===//

using Microsoft.VisualStudio.Editor;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;
using Microsoft.VisualStudio.Text;
using Microsoft.VisualStudio.Text.Editor;
using Microsoft.VisualStudio.TextManager.Interop;
using System;
using System.Collections;
using System.ComponentModel;
using System.ComponentModel.Design;
using System.IO;
using System.Runtime.InteropServices;
using System.Xml.Linq;

namespace LLVM.ClangFormat
{
    [ClassInterface(ClassInterfaceType.AutoDual)]
    [CLSCompliant(false), ComVisible(true)]
    public class OptionPageGrid : DialogPage
    {
        private string assumeFilename = "";
        private string fallbackStyle = "LLVM";
        private bool sortIncludes = false;
        private string style = "file";

        public class StyleConverter : TypeConverter
        {
            protected ArrayList values;
            public StyleConverter()
            {
                // Initializes the standard values list with defaults.
                values = new ArrayList(new string[] { "file", "Chromium", "Google", "LLVM", "Mozilla", "WebKit" });
            }

            public override bool GetStandardValuesSupported(ITypeDescriptorContext context)
            {
                return true;
            }

            public override StandardValuesCollection GetStandardValues(ITypeDescriptorContext context)
            {
                return new StandardValuesCollection(values);
            }

            public override bool CanConvertFrom(ITypeDescriptorContext context, Type sourceType)
            {
                if (sourceType == typeof(string))
                    return true;

                return base.CanConvertFrom(context, sourceType);
            }

            public override object ConvertFrom(ITypeDescriptorContext context, System.Globalization.CultureInfo culture, object value)
            {
                string s = value as string;
                if (s == null)
                    return base.ConvertFrom(context, culture, value);

                return value;
            }
        }

        [Category("LLVM/Clang")]
        [DisplayName("Style")]
        [Description("Coding style, currently supports:\n" +
                     "  - Predefined styles ('LLVM', 'Google', 'Chromium', 'Mozilla', 'WebKit').\n" +
                     "  - 'file' to search for a YAML .clang-format or _clang-format\n" +
                     "    configuration file.\n" +
                     "  - A YAML configuration snippet.\n\n" +
                     "'File':\n" +
                     "  Searches for a .clang-format or _clang-format configuration file\n" +
                     "  in the source file's directory and its parents.\n\n" +
                     "YAML configuration snippet:\n" +
                     "  The content of a .clang-format configuration file, as string.\n" +
                     "  Example: '{BasedOnStyle: \"LLVM\", IndentWidth: 8}'\n\n" +
                     "See also: http://clang.llvm.org/docs/ClangFormatStyleOptions.html.")]
        [TypeConverter(typeof(StyleConverter))]
        public string Style
        {
            get { return style; }
            set { style = value; }
        }

        public sealed class FilenameConverter : TypeConverter
        {
            public override bool CanConvertFrom(ITypeDescriptorContext context, Type sourceType)
            {
                if (sourceType == typeof(string))
                    return true;

                return base.CanConvertFrom(context, sourceType);
            }

            public override object ConvertFrom(ITypeDescriptorContext context, System.Globalization.CultureInfo culture, object value)
            {
                string s = value as string;
                if (s == null)
                    return base.ConvertFrom(context, culture, value);

                // Check if string contains quotes. On Windows, file names cannot contain quotes.
                // We do not accept them however to avoid hard-to-debug problems.
                // A quote in user input would end the parameter quote and so break the command invocation.
                if (s.IndexOf('\"') != -1)
                    throw new NotSupportedException("Filename cannot contain quotes");

                return value;
            }
        }

        [Category("LLVM/Clang")]
        [DisplayName("Assume Filename")]
        [Description("When reading from stdin, clang-format assumes this " +
                     "filename to look for a style config file (with 'file' style) " +
                     "and to determine the language.")]
        [TypeConverter(typeof(FilenameConverter))]
        public string AssumeFilename
        {
            get { return assumeFilename; }
            set { assumeFilename = value; }
        }

        public sealed class FallbackStyleConverter : StyleConverter
        {
            public FallbackStyleConverter()
            {
                // Add "none" to the list of styles.
                values.Insert(0, "none");
            }
        }

        [Category("LLVM/Clang")]
        [DisplayName("Fallback Style")]
        [Description("The name of the predefined style used as a fallback in case clang-format " +
                     "is invoked with 'file' style, but can not find the configuration file.\n" +
                     "Use 'none' fallback style to skip formatting.")]
        [TypeConverter(typeof(FallbackStyleConverter))]
        public string FallbackStyle
        {
            get { return fallbackStyle; }
            set { fallbackStyle = value; }
        }

        [Category("LLVM/Clang")]
        [DisplayName("Sort includes")]
        [Description("Sort touched include lines.\n\n" +
                     "See also: http://clang.llvm.org/docs/ClangFormat.html.")]
        public bool SortIncludes
        {
            get { return sortIncludes; }
            set { sortIncludes = value; }
        }
    }

    [PackageRegistration(UseManagedResourcesOnly = true)]
    [InstalledProductRegistration("#110", "#112", "1.0", IconResourceID = 400)]
    [ProvideMenuResource("Menus.ctmenu", 1)]
    [Guid(GuidList.guidClangFormatPkgString)]
    [ProvideOptionPage(typeof(OptionPageGrid), "LLVM/Clang", "ClangFormat", 0, 0, true)]
    public sealed class ClangFormatPackage : Package
    {
        #region Package Members
        protected override void Initialize()
        {
            base.Initialize();

            var commandService = GetService(typeof(IMenuCommandService)) as OleMenuCommandService;
            if (commandService != null)
            {
                var menuCommandID = new CommandID(GuidList.guidClangFormatCmdSet, (int)PkgCmdIDList.cmdidClangFormat);
                var menuItem = new MenuCommand(MenuItemCallback, menuCommandID);
                commandService.AddCommand(menuItem);
            }
        }
        #endregion

        private void MenuItemCallback(object sender, EventArgs args)
        {
            IWpfTextView view = GetCurrentView();
            if (view == null)
                // We're not in a text view.
                return;
            string text = view.TextBuffer.CurrentSnapshot.GetText();
            int start = view.Selection.Start.Position.GetContainingLine().Start.Position;
            int end = view.Selection.End.Position.GetContainingLine().End.Position;
            int length = end - start;
            // clang-format doesn't support formatting a range that starts at the end
            // of the file.
            if (start >= text.Length && text.Length > 0)
                start = text.Length - 1;
            string path = GetDocumentParent(view);
            string filePath = GetDocumentPath(view);
            try
            {
                var root = XElement.Parse(RunClangFormat(text, start, length, path, filePath));
                var edit = view.TextBuffer.CreateEdit();
                foreach (XElement replacement in root.Descendants("replacement"))
                {
                    var span = new Span(
                        int.Parse(replacement.Attribute("offset").Value),
                        int.Parse(replacement.Attribute("length").Value));
                    edit.Replace(span, replacement.Value);
                }
                edit.Apply();
            }
            catch (Exception e)
            {
                var uiShell = (IVsUIShell)GetService(typeof(SVsUIShell));
                var id = Guid.Empty;
                int result;
                uiShell.ShowMessageBox(
                        0, ref id,
                        "Error while running clang-format:",
                        e.Message,
                        string.Empty, 0,
                        OLEMSGBUTTON.OLEMSGBUTTON_OK,
                        OLEMSGDEFBUTTON.OLEMSGDEFBUTTON_FIRST,
                        OLEMSGICON.OLEMSGICON_INFO,
                        0, out result);
            }
        }

        /// <summary>
        /// Runs the given text through clang-format and returns the replacements as XML.
        /// 
        /// Formats the text range starting at offset of the given length.
        /// </summary>
        private string RunClangFormat(string text, int offset, int length, string path, string filePath)
        {
            string vsixPath = Path.GetDirectoryName(
                typeof(ClangFormatPackage).Assembly.Location);

            System.Diagnostics.Process process = new System.Diagnostics.Process();
            process.StartInfo.UseShellExecute = false;
            process.StartInfo.FileName = vsixPath + "\\clang-format.exe";
            // Poor man's escaping - this will not work when quotes are already escaped
            // in the input (but we don't need more).
            string style = GetStyle().Replace("\"", "\\\"");
            string fallbackStyle = GetFallbackStyle().Replace("\"", "\\\"");
            process.StartInfo.Arguments = " -offset " + offset +
                                          " -length " + length +
                                          " -output-replacements-xml " +
                                          " -style \"" + style + "\"" +
                                          " -fallback-style \"" + fallbackStyle + "\"";
            if (GetSortIncludes())
              process.StartInfo.Arguments += " -sort-includes ";
            string assumeFilename = GetAssumeFilename();
            if (string.IsNullOrEmpty(assumeFilename))
                assumeFilename = filePath;
            if (!string.IsNullOrEmpty(assumeFilename))
              process.StartInfo.Arguments += " -assume-filename \"" + assumeFilename + "\"";
            process.StartInfo.CreateNoWindow = true;
            process.StartInfo.RedirectStandardInput = true;
            process.StartInfo.RedirectStandardOutput = true;
            process.StartInfo.RedirectStandardError = true;
            if (path != null)
                process.StartInfo.WorkingDirectory = path;
            // We have to be careful when communicating via standard input / output,
            // as writes to the buffers will block until they are read from the other side.
            // Thus, we:
            // 1. Start the process - clang-format.exe will start to read the input from the
            //    standard input.
            try
            {
                process.Start();
            }
            catch (Exception e)
            {
                throw new Exception(
                    "Cannot execute " + process.StartInfo.FileName + ".\n\"" + 
                    e.Message + "\".\nPlease make sure it is on the PATH.");
            }
            // 2. We write everything to the standard output - this cannot block, as clang-format
            //    reads the full standard input before analyzing it without writing anything to the
            //    standard output.
            process.StandardInput.Write(text);
            // 3. We notify clang-format that the input is done - after this point clang-format
            //    will start analyzing the input and eventually write the output.
            process.StandardInput.Close();
            // 4. We must read clang-format's output before waiting for it to exit; clang-format
            //    will close the channel by exiting.
            string output = process.StandardOutput.ReadToEnd();
            // 5. clang-format is done, wait until it is fully shut down.
            process.WaitForExit();
            if (process.ExitCode != 0)
            {
                // FIXME: If clang-format writes enough to the standard error stream to block,
                // we will never reach this point; instead, read the standard error asynchronously.
                throw new Exception(process.StandardError.ReadToEnd());
            }
            return output;
        }

        /// <summary>
        /// Returns the currently active view if it is a IWpfTextView.
        /// </summary>
        private IWpfTextView GetCurrentView()
        {
            // The SVsTextManager is a service through which we can get the active view.
            var textManager = (IVsTextManager)Package.GetGlobalService(typeof(SVsTextManager));
            IVsTextView textView;
            textManager.GetActiveView(1, null, out textView);

            // Now we have the active view as IVsTextView, but the text interfaces we need
            // are in the IWpfTextView.
            var userData = (IVsUserData)textView;
            if (userData == null)
                return null;
            Guid guidWpfViewHost = DefGuidList.guidIWpfTextViewHost;
            object host;
            userData.GetData(ref guidWpfViewHost, out host);
            return ((IWpfTextViewHost)host).TextView;
        }

        private string GetStyle()
        {
            var page = (OptionPageGrid)GetDialogPage(typeof(OptionPageGrid));
            return page.Style;
        }

        private string GetAssumeFilename()
        {
            var page = (OptionPageGrid)GetDialogPage(typeof(OptionPageGrid));
            return page.AssumeFilename;
        }

        private string GetFallbackStyle()
        {
            var page = (OptionPageGrid)GetDialogPage(typeof(OptionPageGrid));
            return page.FallbackStyle;
        }

        private bool GetSortIncludes()
        {
            var page = (OptionPageGrid)GetDialogPage(typeof(OptionPageGrid));
            return page.SortIncludes;
        }

        private string GetDocumentParent(IWpfTextView view)
        {
            ITextDocument document;
            if (view.TextBuffer.Properties.TryGetProperty(typeof(ITextDocument), out document))
            {
                return Directory.GetParent(document.FilePath).ToString();
            }
            return null;
        }

        private string GetDocumentPath(IWpfTextView view)
        {
            ITextDocument document;
            if (view.TextBuffer.Properties.TryGetProperty(typeof(ITextDocument), out document))
            {
                return document.FilePath;
            }
            return null;
        }
    }
}
