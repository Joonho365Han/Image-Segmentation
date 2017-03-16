#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <queue>

using namespace std;

struct Vertex
{
	int visited;
	int x;
	int y;
	int d;
};

int main(){

	//  1. Read and construct the maze from the file
	ifstream maze_txt("maze.txt");
	vector<vector<Vertex>> maze;
	for (int i=0; !maze_txt.eof(); i++)
	{
		string buffer = "";
		getline(maze_txt,buffer);

		if (maze_txt.eof()) break;

		vector<Vertex> maze_row;
		for (int j=0; j<buffer.size(); j++)
		{
			Vertex V{(buffer[j] == '0'), i, j, 0};
			maze_row.push_back(V);
		}
		maze.push_back(maze_row);
	}

	//  2. Do BFS
	int N = maze.size();
	queue<Vertex> vertices_to_visit;
	vertices_to_visit.push(maze[0][0]);
	while (!vertices_to_visit.empty())
	{
		Vertex V = vertices_to_visit.front();
		for (int past_col=-1, col=-1, row=0, i=0; i<4; past_col=col, col=row*-1, row=past_col, i++)
		{
			//  Check all 4 vertical and horizontal neighbors.
			int x = V.x+row;
			int y = V.y+col;

			if ((x|y) < 0 || x >= N || y >= N) //  Boundary check
				continue;
			if (maze[x][y].visited)
				continue;

			maze[x][y].visited = true;
			maze[x][y].d = maze[V.x][V.y].d+1;
			vertices_to_visit.push(maze[x][y]);

			if ((x&y)==N-1) //  Reached end
				break;
		}
		vertices_to_visit.pop();
	}

	if (maze[N-1][N-1].visited)
		cout << "The shortest path through the maze is " << maze[N-1][N-1].d << endl;
	else
		cout << "There is no path through the maze...\n";

	return 0;
}