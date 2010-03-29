package desktopsharingprotocol;

import java.io.*;
import java.net.Socket;
import java.nio.charset.Charset;
import java.util.ArrayList;
import java.util.LinkedList;
import java.util.List;

public class DSPConnection {

	public static enum TransferType { UPLOAD, DOWNLOAD };

	private String host;
	private int port = 34900;

	private static final Charset utf8 = Charset.forName("UTF-8");

	private static final int NUMBER_ALLOWED_LENGTH = String.valueOf(Long.MAX_VALUE).length();
	private static final int BUFFER_SIZE = 1024;

	private static byte[] netstring (String what) {
		return (Integer.toString(what.length()) + ":" + what + ",").getBytes(utf8);
	}

	private static byte[] netblob (byte[] blob) {
		String len = Integer.toString(blob.length);
		byte[] res = new byte[len.length() + 1 + blob.length];
		System.arraycopy(len.getBytes(), 0, res, 0, len.length()); // we assume that numbers will be ascii
		res[len.length()] = ':';
		System.arraycopy(blob, 0, res, len.length() + 1, blob.length);
		return res;
	}

	private Socket socket = null;

	public DSPConnection (String host) {
		this.host = host;
	}

	public DSPConnection (String host, int port) {
		this.host = host;
		this.port = port;
	}

	public void connect () throws Exception {
		socket = new Socket(host, port);
	}

	public void disconnect () { }

	private Socket getConnection ()
	throws IllegalStateException {
		if (socket == null || !socket.isConnected()) throw new IllegalStateException("you are not connected");
		return socket;
	}

	synchronized public List<DSPFile> list (String path)
	throws IOException {
		Socket sock = getConnection();
		validatePath(path);
		List<DSPFile> ret = new LinkedList<DSPFile>();

		InputStream is = sock.getInputStream();
		issueCommand(sock, "LIST", netstring(path));
		if (!replyOk(is)) {
			scanErrorReason(is);
			throw new DSPRemoteException("problem in list");
		}

		byte[] rights = new byte[3];
		byte[] date = new byte[20];
		while (true) {
			int len = is.read(rights);
			if (len < 3) throw new IOException("remote host closed connection");
			if (rights[0] == 'E' && rights[1] == 'R' && rights[2] == 'R') {
				scanErrorReason(is);
				throw new DSPRemoteException("problem in list");
			} else if (rights[0] == 'O' && rights[1] == 'K' && rights[2] == ' ') {
				long count = readNumber(is, '\n');
				if (count == ret.size()) return ret;
				else throw new DSPProtocolException();
			}
			// assume that rights are ok, space follows
			if (is.read() != ' ') throw new DSPProtocolException();
			long size = readNumber(is, ' ');
			len = is.read(date);
			if (len < 20) throw new IOException("remote host closed connection");
			if (date[19] != ' ') throw new DSPProtocolException();
			String sdate = new String(date, utf8);
			String name = readNetstring(is);
			if (is.read() != '\n') throw new DSPProtocolException();
			ret.add(new DSPFile(this, path, name, rights, size, sdate));
		}
	}

	public class TransferWorker implements Runnable {
		public final TransferType type;
		public final String remoteName;
		public long total;
		public long transferred = 0;

		private OutputStream to = null;
		private InputStream from = null;

		private boolean failed = false;
		private String failureReason;

		private TransferWorker (String remoteName, OutputStream to) {
			this.type = TransferType.DOWNLOAD;
			this.to = to;
			this.remoteName = remoteName;
		}

		private TransferWorker (String remoteName, InputStream from, long total) {
			this.type = TransferType.UPLOAD;
			this.from = from;
			this.remoteName = remoteName;
			this.total = total;
		}

		private void fail (String why) {
			failed = true;
			failureReason = why;
		}

		public void run () {
			try {
				byte[] buffer = new byte[BUFFER_SIZE];
				Socket sock = getConnection();
				InputStream is = sock.getInputStream();

				if (type == TransferType.DOWNLOAD) issueCommand(sock, "READ", "0", "$", netstring(remoteName));
				else issueCommand(sock, "WRITE", "0", Long.toString(total), netstring(remoteName));

				if (!replyOk(is)) {
					fail(scanErrorReason(is));
					return;
				}

				if (type == TransferType.DOWNLOAD) {
					if (!replyOk(is)) {
						fail(scanErrorReason(is));
						return;
					}

					total = readNumber(is, ':');
					from = is;
				}

				while (transferred < total) {
					int what = (int)Math.min(total - transferred, buffer.length);
					int read = from.read(buffer, 0, what);
					transferred += read;
					if (read > 0) to.write(buffer, 0, read);
					if (read < what) {
						fail("closed connection before transfer completed");
						return;
					}
				}
				if (is.read() != ',') fail("bad protocol");
				if (is.read() != '\n') fail("bad protocol");
			} catch (IOException e) {
				failed = true;
				failureReason = e.getMessage();
			} finally {
				try {
					if (type == TransferType.DOWNLOAD) to.close();
					else from.close();
				} catch (IOException e) { }
			}
		}

		public boolean isFailed() { return failed; }
		public String getFailureReason() { return failureReason; }
	}

	public void download (String path, File where)
	throws IOException {
		validatePath(path);
		boolean exists = where.exists();
		if (!exists) where.createNewFile();
		TransferWorker tw = new TransferWorker(path, new FileOutputStream(where));
		tw.run();
		if (tw.isFailed()) {
			if (!exists && where.length() == 0) where.delete();
			throw new IOException(tw.getFailureReason());
		}
	}
	
	private static void validatePath (String path)
	throws InvalidPathException {
	}

	private static boolean replyOk (InputStream stream)
	throws IOException {
		byte[] b = new byte[3];
		int res = stream.read(b);
			/* read 3 bytes:
				- either "OK " (params continue) or "OK\n" (no params) - caller should know which is the case
				- or "ERR", then scanReason checks for validity */
		if (res < 3) throw new IOException("remote host closed connection");
		if (b[0] == 'O' && b[1] == 'K' && (b[2] == ' ' || b[2] == '\n')) return true;
		else if (b[0] == 'E' && b[1] == 'R' && b[2] == 'R') return false;
		else throw new DSPProtocolException();
	}

	private static String scanErrorReason (InputStream stream)
	throws IOException {
		/* this assumes that "ERR" bytes preceded the call */
		int val = stream.read();
		if (val == -1) throw new IOException("remote host has closed connection");
		else if (val == '\n') return "";
		else if (val != ' ') throw new DSPProtocolException();
		else { // scan the actual reason - should be a netstring
			String reason = readNetstring(stream);
			if (stream.read() != '\n') throw new DSPProtocolException();
			return reason;
		}
	}

	private static String readNetstring (InputStream stream)
	throws IOException {
		long length = readNumber(stream, ':');
		if (length > Integer.MAX_VALUE) throw new DSPProtocolException();
		byte[] buf = new byte[(int)length];
		int actual = stream.read(buf);
		if (actual < length) throw new IOException("remote host has closed connection");
		if (stream.read() != ',') throw new DSPProtocolException();
		return new String(buf, utf8);
	}

	private static long readNumber (InputStream stream, char terminator)
	throws IOException {
		StringBuffer sb = new StringBuffer(NUMBER_ALLOWED_LENGTH); // sane guess for number length
		while (true) {
			int val = stream.read();
			if (val == -1) new IOException("remote host has closed connection");
			else if (val == terminator) break;
			else if (val >= '0' && val <= '9') sb.append((char)val);
			else new DSPProtocolException();
			if (sb.length() >= NUMBER_ALLOWED_LENGTH) throw new DSPProtocolException();
		}
		if (sb.length() == 0 || (sb.charAt(0) == '0' && sb.length() > 1)) throw new DSPProtocolException();
		return Long.valueOf(sb.toString());
	}

	private void issueCommand (Socket sock, Object... args)
	throws IOException {
		int length = 0;
		ArrayList<byte[]> list = new ArrayList<byte[]>(args.length);
		for (Object o : args) {
			byte[] b;
			if (o instanceof byte[]) {
				b = (byte[])o;
			} else {
				b = o.toString().getBytes(utf8);
			}
			length += b.length;
			list.add(b);
		}
		length += list.size();
		byte[] command = new byte[length];
		int pos = 0;
		for (byte[] b : list) {
			System.arraycopy(b, 0, command, pos, b.length);
			pos += b.length;
			command[pos] = ' ';
			pos++;
		}
		command[length-1] = '\n';

		OutputStream os = sock.getOutputStream();
		os.write(command);
		os.flush();
	}
}
