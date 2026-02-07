/* 
Compile: C:/ProgramFiles/Java/jdk-23.0.1/bin/javac.exe NaClCompat.java
Run:     C:/ProgramFiles/Java/jdk-23.0.1/bin/java.exe NaClCompat
*/

import java.security.*;
import java.security.spec.*;
import java.util.Arrays;

public class NaClCompat {
    // secretKey64 = [0..31]=seed, [32..63]=publicKey (TweetNaCl layout)
    public static PrivateKey privateKeyFromTweetNaClSecret(byte[] seed32) throws Exception {
        if (seed32 == null || seed32.length != 32)
            throw new IllegalArgumentException("TweetNaCl seed for key pair must be 32 bytes.");
        KeyFactory kf = KeyFactory.getInstance("Ed25519");
        return kf.generatePrivate(new EdECPrivateKeySpec(NamedParameterSpec.ED25519, seed32));
    }

    public static byte[] cryptoSign(byte[] m, byte[] seed32) throws Exception {
        PrivateKey sk = privateKeyFromTweetNaClSecret(seed32);
        Signature sig = Signature.getInstance("Ed25519"); // RFC 8032 pure Ed25519
        sig.initSign(sk);
        sig.update(m);
        byte[] S = sig.sign(); // 64 bytes

        byte[] sm = new byte[S.length + m.length];
        System.arraycopy(S, 0, sm, 0, 64);
        System.arraycopy(m, 0, sm, 64, m.length);
        return sm;  // NaCl "signed message"
    }
    
    public static void main(String[] args) throws Exception {
        byte[] seed = new byte[32];
        for(byte u=0; u<seed.length; ++u) {
            seed[u] = u;
        }
        
        byte[] msg = new byte[10];
        for(byte u=0; u<9; ++u) {
            msg[u] = u;
            ++ msg[u];
        }
        msg[9] = 0;
        
        System.out.format("Signature: ");
        byte[] signedMsg = cryptoSign(msg, seed);
        for(int i=0; i<signedMsg.length; ++i) {
            System.out.format(" %02X", signedMsg[i]);
        }
        System.out.println("");
    }
}
