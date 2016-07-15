using System;
using System.Text;
using System.Collections.Generic;
using System.Linq;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using System.Net;
using AHttp.Test.Properties;
using System.Threading;

namespace AHttp.Test
{
	/// <summary>
	/// Summary description for CommonFunctionality
	/// </summary>
	[TestClass]
	public class CommonFunctionality
	{
		public CommonFunctionality()
		{
			
		}

		private TestContext _testContextInstance;

		/// <summary>
		///Gets or sets the test context which provides
		///information about and functionality for the current test run.
		///</summary>
		public TestContext TestContext
		{
			get
			{
                return _testContextInstance;
			}
			set
			{
                _testContextInstance = value;
			}
		}

		#region Additional test attributes
		//
		// You can use the following additional attributes as you write your tests:
		//
		// Use ClassInitialize to run code before running the first test in the class
		[ClassInitialize()]
		public static void MyClassInitialize(TestContext testContext) 
        {
            
        }
		//
		// Use ClassCleanup to run code after all tests in a class have run
		// [ClassCleanup()]
		// public static void MyClassCleanup() { }
		//
		// Use TestInitialize to run code before running each test 
		// [TestInitialize()]
		// public void MyTestInitialize() { }
		//
		// Use TestCleanup to run code after each test has run
		// [TestCleanup()]
		// public void MyTestCleanup() { }
		//
		#endregion

        internal class UrlInfo
        {
            public AutoResetEvent Handle { get; set; }
            public string Url { get; set; }
            public string ErrorMessage { get; set; }
        }

        protected void ProcessUrl(object input)
        {
            Assert.IsInstanceOfType(input, typeof(UrlInfo));

            UrlInfo urlInfo = input as UrlInfo;
            urlInfo.ErrorMessage = null;

            try
            {
                WebRequest request = HttpWebRequest.Create(Settings.Default.TargetServer + urlInfo.Url + "?ts=" + DateTime.Now.Ticks);
                request.Timeout = 30 * 1000;
                using (HttpWebResponse response = (HttpWebResponse)request.GetResponse())
                {
                    Assert.AreEqual(response.StatusCode, HttpStatusCode.OK);
                }
                request = null;
            }
            catch (Exception ex)
            {
                urlInfo.ErrorMessage = ex.ToString();
            }
                       
            
            urlInfo.Handle.Set();
        }


		[TestMethod]
        public void SimpleLoadTest()
		{
			string serverName = Settings.Default.TargetServer;
			string[] urls = 
			{
				"/",
				"/js/",
				"/js/atree/img/computer/ico_admin.gif",
				"/js/atree/img/computer/ico_alert.gif",
				"/js/atree/img/computer/ico_computer2.gif",
				"/php/info.php",
				"/upload.html",
			};

            int urlNo = 0;
            UrlInfo[] urlInfos = new UrlInfo[urls.Length];
            AutoResetEvent[] waitHandles = new AutoResetEvent[urls.Length];

            do
            {
                waitHandles[urlNo] = new AutoResetEvent(false);
                urlInfos[urlNo] = new UrlInfo { Handle = waitHandles[urlNo], Url = urls[urlNo] };
                
            } while (++urlNo < urls.Length);
                
			for (int ndx = 0; ndx < 20; ++ndx)
			{
                urlNo = 0;

                foreach (string url in urls)
				{
                    urlInfos[urlNo].Handle.Reset();
                    Thread th = new Thread (ProcessUrl);
                    th.Start (urlInfos[urlNo]);

                    ++urlNo;
				}

                while (urlNo-- > 0)
                {
                    int finishedNdx = WaitHandle.WaitAny(waitHandles, 1000);
                    if (finishedNdx != WaitHandle.WaitTimeout)
                    {
                        Assert.IsTrue(String.IsNullOrEmpty(urlInfos[finishedNdx].ErrorMessage), 
                            "Request failed: " + urlInfos[finishedNdx].ErrorMessage + ", url: " +
                            urlInfos[finishedNdx].Url);
                    }
                }
			}
		}
	}
}
