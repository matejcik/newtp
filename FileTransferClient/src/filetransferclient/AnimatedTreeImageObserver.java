package filetransferclient;

import java.awt.Image;
import java.awt.Rectangle;
import java.awt.image.ImageObserver;
import java.util.LinkedList;
import java.util.List;
import javax.swing.JTree;
import javax.swing.tree.DefaultTreeModel;
import javax.swing.tree.TreeNode;
import javax.swing.tree.TreePath;

public class AnimatedTreeImageObserver implements ImageObserver {

	private static AnimatedTreeImageObserver instance;
	private JTree tree;
	private DefaultTreeModel model;

	private List<TreeNode> nodes;

	private AnimatedTreeImageObserver() {
		nodes = new LinkedList<TreeNode>();
	}

	public static AnimatedTreeImageObserver getInstance() {
		if (instance == null)
			instance = new AnimatedTreeImageObserver();
		return instance;
	}

	public void setTree(JTree tree) {
		this.tree = tree;
		this.model = (DefaultTreeModel)tree.getModel();
	}

	public void observeNode (TreeNode node) {
		nodes.add(node);
	}

	public void unobserveNode (TreeNode node) {
		nodes.remove(node);
	}

	public boolean imageUpdate(Image img, int flags, int x, int y, int width, int height) {
		if ((flags & (FRAMEBITS | ALLBITS)) != 0) {
			for (TreeNode node : nodes) {
				TreeNode[] path = model.getPathToRoot(node);
				if (path == null) continue;
				TreePath tp = new TreePath(path);
				Rectangle rect = tree.getPathBounds(tp);
				if (rect != null) tree.repaint(rect);
			}
		}
		return (flags & (ALLBITS | ABORT)) == 0;
	}

}
