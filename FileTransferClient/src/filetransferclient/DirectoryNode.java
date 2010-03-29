package filetransferclient;

import desktopsharingprotocol.DSPFile;
import java.io.IOException;

public class DirectoryNode extends LoadableNode {
	private DSPFile file;

	public void list () throws IOException {
		files = file.list();
	}

	public DirectoryNode (DSPFile file) {
		super(file.getName());
		this.file = file;
		setConnected(true);
	}

	/**
	 * @return the file
	 */
	public DSPFile getFile() {
		return file;
	}

	/**
	 * @param file the file to set
	 */
	public void setFile(DSPFile file) {
		this.file = file;
	}
}
