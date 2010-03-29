package desktopsharingprotocol;

import java.io.File;
import java.io.IOException;
import java.text.DateFormat;
import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.List;

public class DSPFile implements Comparable<DSPFile> {

	private final DSPConnection parent;
	private final String path;
	private final String name;
	private char[] rights = new char[3];
	private long size;
	private Date mtime;

	private static DateFormat format = new SimpleDateFormat("y-M-d H:m:s ");

	protected DSPFile (DSPConnection parent, String path, String name, byte[] rights, long size, String date)
	{
		this.parent = parent;
		this.path = path;
		this.size = size;
		this.name = name;
		for (int i = 0; i < 3; i++) this.rights[i] = (char)rights[i];
		try {
			mtime = format.parse(date);
		} catch (ParseException e) {
			// bad mtime
			mtime = null;
		}
	}

	public boolean isDirectory () {
		return rights[0] == 'd';
	}

	public List<DSPFile> list ()
	throws DSPRemoteException, IOException {
		if (!isDirectory()) throw new IllegalStateException("this is not a directory");
		return parent.list(path+name+'/');
	}

	public void download (File where)
	throws DSPRemoteException, IOException {
		if (isDirectory()) throw new IllegalStateException("can't download a directory");
	}

	/**
	 * @return the path
	 */
	public String getPath () {
		return path;
	}

	/**
	 * @return the name
	 */
	public String getName () {
		return name;
	}

	/**
	 * @return the rights
	 */
	public char[] getRights () {
		return rights;
	}

	/**
	 * @return the size
	 */
	public long getSize () {
		return size;
	}

	/**
	 * @return the mtime
	 */
	public Date getMtime () {
		return mtime;
	}

	@Override public String toString () { return name; }

	public int compareTo (DSPFile o) {
		if (isDirectory() && !o.isDirectory()) return -1;
		if (!isDirectory() && o.isDirectory()) return 1;
		return getName().compareTo(o.getName());
	}
}
