package filetransferclient;

import desktopsharingprotocol.DSPFile;
import java.io.IOException;
import java.util.List;
import javax.swing.ImageIcon;
import javax.swing.tree.DefaultMutableTreeNode;

abstract public class LoadableNode extends DefaultMutableTreeNode {
	private String name;
	private boolean connected = false;
	protected ImageIcon icon = null;

	public List<DSPFile> files;

	abstract public void list() throws IOException;

	private static final ImageIcon loading = new ImageIcon(LoadableNode.class.getResource("/images/loading.gif"));
	static {
		loading.setImageObserver(AnimatedTreeImageObserver.getInstance());
	}

	public LoadableNode (String name)
	{
		this.name = name;
		setAllowsChildren(true);
		AnimatedTreeImageObserver.getInstance().observeNode(this);
	}

	public LoadableNode (String name, ImageIcon icon)
	{
		this.name = name;
		this.icon = icon;
		setAllowsChildren(true);
		icon.setImageObserver(AnimatedTreeImageObserver.getInstance());
		AnimatedTreeImageObserver.getInstance().observeNode(this);
	}

	public ImageIcon getCustomIcon () {
		if (connected) return icon;
		else return loading;
	}

	@Override public String toString () {
		return getName();
	}

	@Override protected void finalize () {
		AnimatedTreeImageObserver.getInstance().unobserveNode(this);
	}

	/**
	 * @return the name
	 */
	public String getName() {
		return name;
	}

	/**
	 * @param name the name to set
	 */
	public void setName(String name) {
		this.name = name;
	}

	/**
	 * @return the connected
	 */
	public boolean isConnected() {
		return connected;
	}

	/**
	 * @param connected the connected to set
	 */
	public void setConnected(boolean connected) {
		this.connected = connected;
	}
}
