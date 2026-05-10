# bash completion for camview — https://github.com/lenik/picam

_camview_has_still() {
	local w
	for w in "${words[@]:0:cword}"; do
		if [[ $w == -s || $w == --still ]]; then
			return 0
		fi
	done
	return 1
}

_camview_has_duration() {
	local w
	for w in "${words[@]:0:cword}"; do
		if [[ $w == -d || $w == --duration ]]; then
			return 0
		fi
	done
	return 1
}

_camview() {
	local cur prev words cword
	_init_completion || return

	case $prev in
	-d|--duration)
		COMPREPLY=($(compgen -W '5s 10s 30s 1min 2min 500ms 1hr' -- "$cur"))
		return
		;;
	-f|--format)
		if _camview_has_still; then
			COMPREPLY=($(compgen -W 'jpg jpeg png' -- "$cur"))
		elif _camview_has_duration; then
			COMPREPLY=($(compgen -W 'mp4 mov mkv' -- "$cur"))
		else
			COMPREPLY=($(compgen -W 'jpg jpeg png mp4 mov mkv' -- "$cur"))
		fi
		return
		;;
	-i|--device)
		COMPREPLY=($(compgen -W '1 2' -- "$cur"))
		return
		;;
	-o|--output)
		_filedir
		return
		;;
	--camera-name|-r|--resolution|--framerate|--jpeg-quality|--af-settle-ms)
		return
		;;
	esac

	if [[ $cur == -* ]]; then
		local opts='-v -q -h -s -d -f -o -i -r
			--verbose --quiet --help --version
			--still --duration --format --output --device --camera-name
			--resolution --framerate --jpeg-quality --af-settle-ms
			--no-autofocus --headless'
		COMPREPLY=($(compgen -W "$opts" -- "$cur"))
		return
	fi
}

complete -F _camview camview
