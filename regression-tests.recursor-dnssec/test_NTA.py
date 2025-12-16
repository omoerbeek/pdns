import dns
import json
import os
import requests
import urllib
from recursortests import RecursorTest

class NTATest(RecursorTest):
    _confdir = 'NTA'
    _auth_zones = RecursorTest._default_auth_zones

    _config_template = """dnssec=validate"""
    _lua_config_file = """addNTA("bogus.example")
addNTA('secure.optout.example', 'Should be Insecure, even with DS configured')
addTA('secure.optout.example', '64215 13 1 b88284d7a8d8605c398e8942262f97b9a5a31787')"""

    def testDirectNTA(self):
        """Ensure a direct query to a bogus name with an NTA is Insecure"""

        msg = dns.message.make_query("ted.bogus.example.", dns.rdatatype.A)
        msg.flags = dns.flags.from_text('AD RD')
        msg.use_edns(edns=0, ednsflags=dns.flags.edns_from_text('DO'))

        res = self.sendUDPQuery(msg)

        self.assertMessageHasFlags(res, ['QR', 'RA', 'RD'], ['DO'])
        self.assertRcodeEqual(res, dns.rcode.NOERROR)

    def testCNAMENTA(self):
        """Ensure a CNAME from a secure zone to a bogus one with an NTA is Insecure"""
        msg = dns.message.make_query("cname-to-bogus.secure.example.", dns.rdatatype.A)
        msg.flags = dns.flags.from_text('AD RD')
        msg.use_edns(edns=0, ednsflags=dns.flags.edns_from_text('DO'))

        res = self.sendUDPQuery(msg)

        self.assertMessageHasFlags(res, ['QR', 'RA', 'RD'], ['DO'])
        self.assertRcodeEqual(res, dns.rcode.NOERROR)

    def testSecureWithNTAandDS(self):
        """#4391: when there is a TA *and* NTA configured for a name, the result must be insecure"""
        msg = dns.message.make_query("node1.secure.optout.example.", dns.rdatatype.A)
        msg.flags = dns.flags.from_text('AD RD')
        msg.use_edns(edns=0, ednsflags=dns.flags.edns_from_text('DO'))

        res = self.sendUDPQuery(msg)

        self.assertMessageHasFlags(res, ['QR', 'RA', 'RD'], ['DO'])
        self.assertRcodeEqual(res, dns.rcode.NOERROR)

class NTARESTTest(RecursorTest):
    _confdir = 'NTAREST'
    _wsPort = 8042
    _wsTimeout = 2
    _wsPassword = 'secretpassword'
    _apiKey = 'secretapikey'

    _config_template = """
dnssec:
    validation: validate
    negative_trustanchors:
    - name: bogus.example
    - name: secure.optout.example
      reason: 'With a reason'

webservice:
    loglevel: detailed
    webserver: true
    port: %d
    address: 127.0.0.1
    password: %s
    allow_from: [127.0.0.1]
    api_key: %s
""" % (_wsPort, _wsPassword, _apiKey)

    @classmethod
    def generateRecursorConfig(cls, confdir):
        super(NTARESTTest, cls).generateRecursorYamlConfig(confdir, False)

    def getNTAs(self):
        headers = {'x-api-key': self._apiKey}
        url = 'http://127.0.0.1:' + str(self._wsPort) + '/api/v1/servers/localhost/negativetrustanchors'
        r = requests.get(url, headers=headers, timeout=self._wsTimeout)
        self.assertTrue(r)
        self.assertEqual(r.status_code, 200)
        content = r.json()
        return content

    def addNTA(self, name, reason):
        headers = {'x-api-key': self._apiKey, 'content-type': 'application/json'}
        data = {
            'name': name,
            'why': reason
        }
        url = 'http://127.0.0.1:' + str(self._wsPort) + '/api/v1/servers/localhost/negativetrustanchors';
        r = requests.post(url, headers=headers, data=json.dumps(data), timeout=self._wsTimeout)
        self.assertTrue(r)
        self.assertEqual(r.status_code, 201)
        content = r.json()
        return content

    def delNTA(self, name):
        headers = {'x-api-key': self._apiKey}
        url = 'http://127.0.0.1:' + str(self._wsPort) + '/api/v1/servers/localhost/negativetrustanchors/' + name;
        r = requests.delete(url, headers=headers, timeout=self._wsTimeout)
        self.assertTrue(r)
        self.assertEqual(r.status_code, 204)
        return r

    def testModAndRestart(self):
        confdir = os.path.join('configs', self._confdir)
        self.waitForTCPSocket("127.0.0.1", self._wsPort)
        content = self.getNTAs()
        self.assertEqual(len(content), 2)
        self.delNTA('secure.optout.example')
        self.delNTA('bogus.example')
        content = self.getNTAs()
        self.assertEqual(len(content), 0)
        nta = self.addNTA('=secure.optout.example.', 'reason')
        self.assertIn('name', nta)
        self.assertIn('id', nta)
        self.assertIn('reason', nta)
        self.assertEqual(nta['id'], '=3Dsecure.optout.example.')
        content = self.getNTAs()
        self.assertEqual(len(content), 1)
        nta = content[0]
        self.assertIn('name', nta)
        self.assertIn('id', nta)
        self.assertIn('reason', nta)
        self.assertEqual(nta['id'], '=3Dsecure.optout.example.')
        self.recControl(confdir, 'reload-yaml')
        content = self.getNTAs()
        self.assertEqual(len(content), 1)
        nta = content[0]
        self.assertIn('name', nta)
        self.assertIn('id', nta)
        self.assertIn('reason', nta)
        self.assertEqual(nta['id'], '=3Dsecure.optout.example.')
        self.recControl(confdir, 'reload-yaml reset')
        content = self.getNTAs()
        self.assertEqual(len(content), 2) # the two from config
        nta = content[0]
        self.assertIn('name', nta)
        self.assertIn('id', nta)
        self.assertIn('reason', nta)
        self.assertEqual(nta['name'], 'bogus.example.') # we know it's a (sorted) map
