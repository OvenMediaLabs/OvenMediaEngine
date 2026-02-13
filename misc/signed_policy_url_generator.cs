using System;
using System.Collections.Generic;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using static System.Net.Mime.MediaTypeNames;

namespace OvenSignedPolicyUrl
{
    public class signed_policy_url_generator
    {
        static readonly char[] padding = { '=' };
        public string base_url { get; set; }
        public string secret_key { get; set; }
        public signed_policy_url_generator(string baseUrl, string secretKey)
        {
            base_url = baseUrl;
            secret_key = secretKey;
        }
        public virtual string make_digest(string message)
        {
            using (var hmacsha1 = new HMACSHA1(Encoding.UTF8.GetBytes(secret_key)))
            {
                var hash = hmacsha1.ComputeHash(Encoding.UTF8.GetBytes(message));
                return Convert.ToBase64String(hash)
                    .TrimEnd(padding).Replace('+', '-').Replace('/', '_');
            }
        }
        public virtual string generate(long url_expire
            , long? url_activate
            , long? stream_expire
            , string allow_ip)
        {
            var policy_dict = new Dictionary<string, object>();
            policy_dict.Add("url_expire", url_expire);
            if (url_activate != null)
                policy_dict.Add("url_activate", url_activate);
            if (stream_expire != null)
                policy_dict.Add("stream_expire", stream_expire);
            if (!string.IsNullOrEmpty(allow_ip))
                policy_dict.Add("allow_ip", allow_ip);
            string policy_json = new JavaScriptSerializer().Serialize(policy_dict);
            
            var policy_base64 = System.Convert.ToBase64String(Encoding.UTF8.GetBytes(policy_json)).TrimEnd(padding).Replace('+', '-').Replace('/', '_');
            //Check if parameter present in url 
            if (base_url.Contains("?")) { base_url = base_url + "&"; }
            else { base_url = base_url + "?"; }
            var stream_url = string.Format("{0}policy={1}", base_url, policy_base64);
            var sig = make_digest(stream_url);
            
            return string.Format("{0}&signature={1}", stream_url, sig);
        }

        public string DecodeBase64(string value)
        {
            if (string.IsNullOrEmpty(value))
                return string.Empty;
            var valueBytes = System.Convert.FromBase64String(value);
            return System.Text.Encoding.UTF8.GetString(valueBytes);
        }
    }
}
